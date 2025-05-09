#version 460
#pragma shader_stage(compute)

/*────────────────────────  CS layout  ────────────────────────*/
layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

layout(binding = 0, rgba8) uniform writeonly image2D outImg;

layout(std430, binding = 1) readonly buffer BranchBuf  { float br[];      };
layout(std430, binding = 2) readonly buffer BvhNodeBuf { float nodeData[]; };
layout(std430, binding = 3) readonly buffer LeafIdxBuf { uint  leafIdx[];  };

/*────────────────────────  push‑constants  ───────────────────*/
layout(push_constant) uniform PC {
    vec4 camPos, camR, camU, camF;   // camera basis
    vec4 scr;                        // (W, H, numBranches, maxBFS)
    vec4 flags;                      // x = vis‑mode  (0=shade,1=BFS,2=test)
                                      // y > 0.5  → skeleton overlay
} pc;

/*────────────────────────  helpers  ──────────────────────────*/
struct Branch { vec3 s; float r; vec3 e; float bfs; };
Branch branch(uint i)
{
    uint o = i * 9u;
    return Branch(vec3(br[o + 0], br[o + 1], br[o + 2]),
                  br[o + 3],
                  vec3(br[o + 4], br[o + 5], br[o + 6]),
                  br[o + 7]);
}

struct Node { vec3 mn; vec3 mx; uint lo; uint hi; bool leaf; };
Node node(uint i)
{
    uint o = i * 8u;
    vec3 mn = vec3(nodeData[o + 0], nodeData[o + 1], nodeData[o + 2]);
    vec3 mx = vec3(nodeData[o + 3], nodeData[o + 4], nodeData[o + 5]);
    uint lo = floatBitsToUint(nodeData[o + 6]);
    uint hi = floatBitsToUint(nodeData[o + 7]);
    bool lf = (hi & 0x80000000u) != 0u;
    hi &= 0x7fffffffu;
    return Node(mn, mx, lo, hi, lf);
}

float sdAABB(vec3 p, vec3 mn, vec3 mx)
{
    vec3 q = max(mn - p, p - mx);
    return length(max(q, 0.0)) + min(max(q.x, max(q.y, q.z)), 0.0);
}

float sdCyl(vec3 p, vec3 a, vec3 b, float r)
{
    vec3 ab = b - a;
    float t = clamp(dot(p - a, ab) / dot(ab, ab), 0.0, 1.0);
    vec3 q = a + ab * t;
    return length(p - q) - r;
}

/* IQ smooth‑min */
float smin(float a, float b, float k)
{
    float h = clamp(0.5 + 0.5 * (b - a) / k, 0.0, 1.0);
    return mix(b, a, h) - k * h * (1.0 - h);
}

/*────────────────────────  scene SDF  ────────────────────────*/
float sceneSDF(vec3 p, out float bfs, out float tests)
{
    bfs = 0.0; tests = 0.0;
    float d = 1e9;

    const float kBlend = 0.005;

    uint stack[64]; int sp = 0; stack[sp++] = 0u;

    while (sp > 0)
    {
        uint ni = stack[--sp];
        tests += 1.0;

        Node nd = node(ni);
        float dn = sdAABB(p, nd.mn, nd.mx);
        if (dn > d) continue;

        if (nd.leaf)
        {
            for (uint i = 0u; i < nd.hi; ++i)
            {
                tests += 1.0;
                Branch b = branch(leafIdx[nd.lo + i]);
                float dc = sdCyl(p, b.s, b.e, b.r);
                d = smin(d, dc, kBlend);
                if (d == dc) bfs = b.bfs;
            }
        }
        else { stack[sp++] = nd.lo; stack[sp++] = nd.hi; }
    }
    return d;
}

/*────────────────────────  ray‑march  ────────────────────────*/
struct Trace { float t, cnt; };
Trace raymarch(vec3 ro, vec3 rd)
{
    const int STEPS = 64; const float FAR = 50.0; const float EPS = 0.001;
    float t = 0.0, tot = 0.0;
    for (int i = 0; i < STEPS; ++i)
    {
        float bfs, tests; float d = sceneSDF(ro + rd * t, bfs, tests);
        tot += tests;
        if (d < EPS) return Trace(t, tot);
        t += d; if (t > FAR) break;
    }
    return Trace(FAR, tot);
}

/*──────────────────  surface normal (central diff)  ──────────*/
vec3 normalAt(vec3 p)
{
    vec2 e = vec2(0.001, 0.0); float b, t;
    float d0 = sceneSDF(p, b, t);
    float dx = sceneSDF(p + vec3(e.x, e.y, e.y), b, t)
             - sceneSDF(p - vec3(e.x, e.y, e.y), b, t);
    float dy = sceneSDF(p + vec3(e.y, e.x, e.y), b, t)
             - sceneSDF(p - vec3(e.y, e.x, e.y), b, t);
    float dz = sceneSDF(p + vec3(e.y, e.y, e.x), b, t) - d0;
    return normalize(vec3(dx, dy, dz));
}

/*────────────────────────  shading  ──────────────────────────*/
vec3 shade(vec3 p, vec3 n)
{
    vec3 base = vec3(0.4, 0.3, 0.2);
    vec3 L = normalize(vec3(1, 1, -0.5));
    vec3 V = normalize(pc.camPos.xyz - p);
    vec3 H = normalize(L + V);

    vec3 amb = 0.15 * base;
    vec3 dif = 0.75 * base * max(dot(n, L), 0.0);
    float sp = pow(max(dot(n, H), 0.0), 16.0) * 0.2;
    return amb + dif + vec3(sp);
}

/*──────── distance between a ray and a segment (for skeleton) ─────────*/
float raySegDist(vec3 ro, vec3 rd, vec3 a, vec3 b)
{
    vec3 ab = b - a;
    float ab2 = dot(ab, ab);
    float rd_ab = dot(rd, ab);

    float denom = ab2 - rd_ab * rd_ab;
    float s = 0.0, t = 0.0;
    vec3 ao = ro - a;

    if (denom > 1e-6)
    {
        float ao_ab = dot(ao, ab);
        float ao_rd = dot(ao, rd);
        s = clamp((ao_ab * rd_ab - ao_rd * ab2) / denom, 0.0, 1.0);
        t = clamp((ao_ab + s * rd_ab) / ab2, 0.0, 1.0);
    }

    vec3 pRay = ro + rd * s;
    vec3 pSeg = a + ab * t;
    return length(pRay - pSeg);
}

/*────────────────────────  main()  ───────────────────────────*/
void main()
{
    ivec2 gid = ivec2(gl_GlobalInvocationID.xy);
    float W = pc.scr.x, H = pc.scr.y;
    if (gid.x >= int(W) || gid.y >= int(H)) return;

    vec2 uv = (vec2(gid) + 0.5) / vec2(W, H);
    uv.y = 1.0 - uv.y;
    vec2 ndc = uv * 2.0 - 1.0; ndc.x *= W / H;

    vec3 ro = pc.camPos.xyz;
    vec3 rd = normalize(pc.camF.xyz + ndc.x * pc.camR.xyz + ndc.y * pc.camU.xyz);

    /*──── optional wire‑frame overlay – before heavy marching ────*/
    if (pc.flags.y > 0.5)
    {
        float dMin = 1e9;
        uint brCnt = uint(pc.scr.z);
        for (uint i = 0u; i < brCnt; ++i)
        {
            Branch b = branch(i);
            dMin = min(dMin, raySegDist(ro, rd, b.s, b.e) - b.r);
        }
        if (dMin < 0.003) {            // 3 mm screen‑space width
            imageStore(outImg, gid, vec4(1.0));  // white line
            return;
        }
    }

    Trace tr = raymarch(ro, rd);

    /* background */
    if (tr.t > 49.9) {
        imageStore(outImg, gid, vec4(0.0, 0.15, 0.2, 1.0));
        return;
    }

    vec3 pos = ro + rd * tr.t;
    float bfs, tmp; sceneSDF(pos, bfs, tmp);
    vec3 n = normalAt(pos);

    float mode = pc.flags.x;
    if (mode >= 2.5)               /* test‑count heat‑map          */
    {
        float m = clamp(tr.cnt / pc.scr.z, 0.0, 1.0);
        imageStore(outImg, gid, vec4(m, 0.0, 1.0 - m, 1.0));
    }
    else if (mode >= 1.5)          /* BFS depth visualisation      */
    {
        float c = clamp(bfs / pc.scr.w, 0.0, 1.0);
        imageStore(outImg, gid, vec4(c, 1.0 - c, 0.0, 1.0));
    }
    else                           /* regular shading              */
    {
        vec3 col = shade(pos, n);
        imageStore(outImg, gid, vec4(col, 1.0));
    }
}
