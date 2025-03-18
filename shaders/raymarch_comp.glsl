#version 450

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(binding = 0, rgba8) uniform image2D outImage;

// Branch data is stored as 9 floats per branch:
//   [0..2] : full-grown start position
//   [3]    : radius
//   [4..6] : full-grown end position
//   [7]    : bfsDepth (for debug purposes)
//   [8]    : parentIndex stored as float bits (-1 becomes 0xffffffff)
layout(std430, binding = 1) buffer OldBuf {
    float oldData[];
};
layout(std430, binding = 2) buffer NewBuf {
    float newData[];
};

// Push constants:
//   row0: (time, globalAlpha, width, height)
//   row1: (numOldBranches, numNewBranches, maxBFS, debugToggle)
//    debugToggle = 1.0 means use debug coloring for plant branches,
//                  0.0 means use normal shading.
layout(push_constant) uniform PC {
    vec4 row0;
    vec4 row1;
} pc;

struct Branch {
    vec3 start;
    float radius;
    vec3 end;
    float bfsDepth;
    int parentIndex;
};

Branch getOldBranch(int i) {
    int b = i * 9;
    Branch br;
    br.start    = vec3(oldData[b + 0], oldData[b + 1], oldData[b + 2]);
    br.radius   = oldData[b + 3];
    br.end      = vec3(oldData[b + 4], oldData[b + 5], oldData[b + 6]);
    br.bfsDepth = oldData[b + 7];
    uint bits   = floatBitsToUint(oldData[b + 8]);
    br.parentIndex = (bits == 0xffffffffu) ? -1 : int(bits);
    return br;
}

Branch getNewBranch(int i) {
    int b = i * 9;
    Branch br;
    br.start    = vec3(newData[b + 0], newData[b + 1], newData[b + 2]);
    br.radius   = newData[b + 3];
    br.end      = vec3(newData[b + 4], newData[b + 5], newData[b + 6]);
    br.bfsDepth = newData[b + 7];
    uint bits   = floatBitsToUint(newData[b + 8]);
    br.parentIndex = (bits == 0xffffffffu) ? -1 : int(bits);
    return br;
}

// Iteratively compute the hierarchical partial endpoint for a branch’s parent.
// This avoids recursion by walking up the parent chain (up to a fixed maximum depth).
vec3 computeHierarchicalPartialEnd(int branchIndex, float globalAlpha) {
    float a = clamp(globalAlpha, 0.0, 1.0);
    const int MAX_DEPTH = 32;
    int chain[MAX_DEPTH];
    int count = 0;
    int cur = branchIndex;
    while(cur >= 0 && count < MAX_DEPTH) {
        chain[count] = cur;
        count++;
        Branch b = getNewBranch(cur);
        cur = b.parentIndex;
    }
    if(count == 0)
        return vec3(0.0);
    // Start with the root branch's partial endpoint.
    Branch root = getNewBranch(chain[count - 1]);
    vec3 partialEnd = root.start + a * (root.end - root.start);
    // Accumulate each branch's offset.
    for (int i = count - 2; i >= 0; i--) {
        Branch b = getNewBranch(chain[i]);
        partialEnd += a * (b.end - b.start);
    }
    return partialEnd;
}

float sdfCyl(vec3 p, vec3 A, vec3 B, float r) {
    vec3 AB = B - A;
    float ab2 = dot(AB, AB);
    if (ab2 < 1e-8)
        return length(p - A) - r;
    vec3 AP = p - A;
    float t = clamp(dot(AP, AB)/ab2, 0.0, 1.0);
    vec3 closest = A + t * AB;
    return length(p - closest) - r;
}

float smin(float d1, float d2, float k) {
    float h = clamp(0.5 + 0.5*(d2-d1)/k, 0.0, 1.0);
    return mix(d2, d1, h) - k*h*(1.0-h);
}

float sdfPlane(vec3 p) {
    return p.y + 1.0;
}

//
// sceneSDF: for old branches we use their full endpoints.
// For new branches, if the branch has a parent, we anchor its start to the parent's current partial end.
// Then we compute the branch's partial end using the uniform globalAlpha.
//
float sceneSDF(vec3 p) {
    float globalAlpha = pc.row0.y;
    float numOld = pc.row1.x;
    float numNew = pc.row1.y;
    
    float distVal = 99999.0;
    float k = 0.02;
    
    // Fully grown old branches.
    for (int i = 0; i < int(numOld); i++) {
        Branch bo = getOldBranch(i);
        float d = sdfCyl(p, bo.start, bo.end, bo.radius);
        distVal = smin(distVal, d, k);
    }
    
    // New branches with partial growth.
    for (int i = 0; i < int(numNew); i++) {
        Branch bn = getNewBranch(i);
        float a = clamp(globalAlpha, 0.0, 1.0);
        vec3 branchAnchor = bn.start;
        if (bn.parentIndex >= 0) {
            branchAnchor = computeHierarchicalPartialEnd(bn.parentIndex, globalAlpha);
        }
        vec3 branchEnd = branchAnchor + a * (bn.end - bn.start);
        float currentRadius = bn.radius * a;
        float d = sdfCyl(p, branchAnchor, branchEnd, currentRadius);
        distVal = smin(distVal, d, k);
    }
    
    // Ground plane.
    float dp = sdfPlane(p);
    distVal = smin(distVal, dp, 0.03);
    return distVal;
}

float raymarch(vec3 ro, vec3 rd) {
    float t = 0.0;
    const float FAR = 50.0;
    const int STEPS = 100;
    const float EPS = 0.001;
    for (int i = 0; i < STEPS; i++) {
        vec3 pos = ro + rd * t;
        float d = sceneSDF(pos);
        if (d < EPS)
            return t;
        t += d;
        if (t > FAR)
            break;
    }
    return FAR;
}

vec3 calcNormal(vec3 p) {
    vec2 e = vec2(0.0005, 0);
    float d0 = sceneSDF(p + vec3(e.x, e.y, e.y));
    float d1 = sceneSDF(p - vec3(e.x, e.y, e.y));
    float d2 = sceneSDF(p + vec3(e.y, e.x, e.y));
    float d3 = sceneSDF(p - vec3(e.y, e.x, e.y));
    float d4 = sceneSDF(p + vec3(e.y, e.y, e.x));
    float d5 = sceneSDF(p - vec3(e.y, e.y, e.x));
    return normalize(vec3(d0-d1, d2-d3, d4-d5));
}

vec3 shade(vec3 p, vec3 n) {
    vec3 lightDir = normalize(vec3(1.0, 1.0, -0.5));
    float diff = max(dot(n, lightDir), 0.0);
    vec3 base = vec3(0.4, 0.3, 0.2);
    return base * (0.2 + 0.8 * diff);
}

//
// Debug helpers: We use getDebugBranchDepth() to compute a branch depth (BFS depth)
// for the branch closest (by SDF distance) to the hit point, then map it to a color.
// For the ground plane, we always use normal shading.
//
float getDebugBranchDepth(vec3 p) {
    float bestDist = 99999.0;
    float bestDepth = 0.0;
    int numOld = int(pc.row1.x);
    for (int i = 0; i < numOld; i++) {
        Branch bo = getOldBranch(i);
        float d = abs(sdfCyl(p, bo.start, bo.end, bo.radius));
        if (d < bestDist) {
            bestDist = d;
            bestDepth = 0.0;
        }
    }
    int numNew = int(pc.row1.y);
    for (int i = 0; i < numNew; i++) {
        Branch bn = getNewBranch(i);
        vec3 anchor = bn.start;
        if (bn.parentIndex >= 0)
            anchor = computeHierarchicalPartialEnd(bn.parentIndex, pc.row0.y);
        vec3 branchPE = anchor + pc.row0.y * (bn.end - bn.start);
        float d = abs(sdfCyl(p, anchor, branchPE, bn.radius * pc.row0.y));
        if (d < bestDist) {
            bestDist = d;
            bestDepth = bn.bfsDepth;
        }
    }
    return bestDepth;
}

vec3 depthToColor(float depth, float maxDepth) {
    float t = clamp(depth / maxDepth, 0.0, 1.0);
    return mix(vec3(0.0, 1.0, 0.0), vec3(1.0, 0.0, 0.0), t);
}

//
// Main compute shader entry point.
// If debugToggle (pc.row1.w) is enabled (>=0.5) and the hit point is not on the ground,
// output the debug color (based on branch depth); otherwise, use normal shading.
//
void main() {
    ivec2 gid = ivec2(gl_GlobalInvocationID.xy);
    float W = pc.row0.z;
    float H = pc.row0.w;
    if (gid.x >= int(W) || gid.y >= int(H))
        return;

    vec2 uv = (vec2(gid) + 0.5) / vec2(W, H);
    uv.y = 1.0 - uv.y;
    vec2 ndc = uv * 2.0 - 1.0;
    ndc.x *= (W/H);

    float time = pc.row0.x;
    float globalAlpha = pc.row0.y;
    vec3 ro = vec3(0, 1.2, 3.0);
    vec3 rd = normalize(vec3(ndc.x, ndc.y - 0.2, -1.0));

    float angle = time * 0.5;
    float c = cos(angle), s = sin(angle);
    mat2 r = mat2(c, -s, s, c);
    vec2 roXZ = r * ro.xz;
    ro.x = roXZ.x; ro.z = roXZ.y;
    vec2 rdXZ = r * rd.xz;
    rd.x = rdXZ.x; rd.z = rdXZ.y;

    float tHit = raymarch(ro, rd);
    if (tHit > 49.9) {
        imageStore(outImage, gid, vec4(0.0, 0.15, 0.2, 1.0));
        return;
    }
    vec3 p = ro + rd * tHit;
    vec3 n = calcNormal(p);

    // Always use normal shading for the ground plane.
    if (p.y < -0.98) {
        imageStore(outImage, gid, vec4(shade(p, n), 1.0));
        return;
    }

    // Check the debug toggle (pc.row1.w): if enabled, output debug color; otherwise normal shading.
    if (pc.row1.w >= 0.5) {
        float depth = getDebugBranchDepth(p);
        float maxDepth = pc.row1.z;
        vec3 debugColor = depthToColor(depth, maxDepth);
        imageStore(outImage, gid, vec4(debugColor, 1.0));
    } else {
        imageStore(outImage, gid, vec4(shade(p, n), 1.0));
    }
}
