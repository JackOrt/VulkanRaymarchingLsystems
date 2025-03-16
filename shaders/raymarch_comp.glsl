#version 450

layout(local_size_x=8, local_size_y=8, local_size_z=1) in;

// We write the final result here
layout(binding=0, rgba8) uniform image2D outImage;

struct Branch {
    vec3 start;
    float radius;
    vec3 end;
    float bfsDepth;
};

layout(std430, binding=1) buffer OldBuf {
    Branch oldBranches[];
};
layout(std430, binding=2) buffer NewBuf {
    Branch newBranches[];
};

// push constants => 2x vec4 => 32 bytes
// row0 => (time, alpha, w,h)
// row1 => (#old, #new, maxBFS, 0)
layout(push_constant) uniform PC {
    vec4 row0;
    vec4 row1;
} pc;

float sdfCyl(vec3 p, vec3 A, vec3 B, float r)
{
    vec3 AB= B-A;
    vec3 AP= p-A;
    float t= clamp(dot(AP,AB)/dot(AB,AB),0.0,1.0);
    vec3 closest= A + t*AB;
    return length(p-closest)-r;
}

float smin(float d1, float d2, float k)
{
    float h= clamp(0.5+0.5*(d2-d1)/k,0.0,1.0);
    return mix(d2,d1,h) - k*h*(1.0-h);
}

float sdfPlane(vec3 p)
{
    return p.y+1.0;
}

float sceneSDF(vec3 p)
{
    float time= pc.row0.x;
    float globalAlpha= pc.row0.y; // [0..1]
    float w= pc.row0.z;
    float h= pc.row0.w;

    float oldCount= pc.row1.x;
    float newCount= pc.row1.y;
    float maxBFS= pc.row1.z;

    float distVal= 99999.0;
    float k=0.02;

    // old => fully sized
    for(int i=0; i<int(oldCount); i++){
        Branch bo= oldBranches[i];
        float d= sdfCyl(p, bo.start, bo.end, bo.radius);
        distVal= smin(distVal,d,k);
    }

    // new => BFS overlap
    // localAlpha= clamp( globalAlpha - BFSdepth/(maxBFS+1), 0,1 )
    // => BFS=0 is localAlpha=globalAlpha, BFS=1 => localAlpha= alpha - 1/(maxBFS+1)
    for(int i=0;i<int(newCount);i++){
        Branch bn= newBranches[i];
        float BFSd= bn.bfsDepth;
        float localAlpha= globalAlpha - BFSd/(maxBFS+1.0);
        if(localAlpha<0.0) continue;
        if(localAlpha>1.0) localAlpha=1.0;

        vec3 seg= bn.end - bn.start;
        vec3 partialEnd= bn.start + localAlpha* seg;
        float rr= bn.radius* localAlpha;

        float d= sdfCyl(p, bn.start, partialEnd, rr);
        distVal= smin(distVal,d,k);
    }

    float dp= sdfPlane(p);
    distVal= smin(distVal,dp,0.03);
    return distVal;
}

float raymarch(vec3 ro, vec3 rd)
{
    float t=0.0;
    const float FAR=50.;
    const int STEPS=100;
    const float EPS=0.001;
    for(int i=0;i<STEPS;i++){
        vec3 pos= ro+ rd*t;
        float d= sceneSDF(pos);
        if(d<EPS) return t;
        t+= d;
        if(t> FAR) break;
    }
    return FAR;
}

vec3 calcNormal(vec3 p)
{
    vec2 e=vec2(0.0005,0);
    float d0= sceneSDF(p+vec3(e.x,e.y,e.y));
    float d1= sceneSDF(p-vec3(e.x,e.y,e.y));
    float d2= sceneSDF(p+vec3(e.y,e.x,e.y));
    float d3= sceneSDF(p-vec3(e.y,e.x,e.y));
    float d4= sceneSDF(p+vec3(e.y,e.y,e.x));
    float d5= sceneSDF(p-vec3(e.y,e.y,e.x));
    return normalize(vec3(d0-d1, d2-d3, d4-d5));
}

vec3 shade(vec3 p, vec3 n)
{
    vec3 lightDir= normalize(vec3(1.0,1.0,-0.5));
    float diff= max(dot(n, lightDir), 0.0);
    vec3 base= vec3(0.4,0.3,0.2);
    vec3 col= base*(0.2+ 0.8*diff);
    if(p.y< -1.0) col*=0.1;
    return col;
}

void main()
{
    ivec2 gid= ivec2(gl_GlobalInvocationID.xy);
    float W= pc.row0.z;
    float H= pc.row0.w;
    if(gid.x>= int(W)|| gid.y>= int(H)) return;

    // flip y
    vec2 uv= (vec2(gid)+0.5)/ vec2(W,H);
    uv.y=1.0- uv.y;
    vec2 ndc= uv*2.0-1.0;
    ndc.x*= (W/H);

    float time= pc.row0.x;
    float alpha= pc.row0.y;
    vec3 ro= vec3(0,1.2,3.0);
    vec3 rd= normalize(vec3(ndc.x, ndc.y-0.2, -1.0));

    // rotate around Y
    float angle= time*0.5;
    float c= cos(angle), s= sin(angle);
    mat2 r= mat2(c,-s,s,c);
    vec2 roXZ= r* ro.xz;
    ro.x= roXZ.x; ro.z= roXZ.y;
    vec2 rdXZ= r* rd.xz;
    rd.x= rdXZ.x; rd.z= rdXZ.y;

    float tHit= raymarch(ro,rd);
    if(tHit>49.9){
        imageStore(outImage,gid, vec4(0.0,0.15,0.2,1.0));
        return;
    }
    vec3 p= ro+ rd*tHit;
    vec3 n= calcNormal(p);
    vec3 color= shade(p,n);
    imageStore(outImage,gid, vec4(color,1.0));
}
