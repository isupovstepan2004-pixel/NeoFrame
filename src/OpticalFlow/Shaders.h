#pragma once
// All HLSL for luma pyramid, hierarchical block matching, vector median filter,
// and motion-compensated interpolation. Compiled at runtime with D3DCompile.
namespace nf {

inline constexpr const char* kShaderSource = R"HLSL(
cbuffer CB : register(b0)
{
    uint4  sizes;   // srcW, srcH, l1W, l1H
    uint4  sizes2;  // l2W, l2H, coarseGridW, coarseGridH
    uint4  sizes3;  // fineGridW, fineGridH, refineRadius, useMotion
    float4 params;  // x = t (0..1), y = confidence range
};

Texture2D<float4> gSrc      : register(t0);
Texture2D<float>  gLumaIn   : register(t1);
Texture2D<float>  gL2Cur    : register(t2);
Texture2D<float>  gL2Prev   : register(t3);
Texture2D<float>  gL1Cur    : register(t4);
Texture2D<float>  gL1Prev   : register(t5);
StructuredBuffer<float4> gCoarse : register(t6);
StructuredBuffer<float4> gMVIn   : register(t7);
Texture2D<float4> gColPrev  : register(t8);
Texture2D<float4> gColCur   : register(t9);
StructuredBuffer<float4> gMV     : register(t10);
RWTexture2D<float> gLumaOut : register(u0);
RWStructuredBuffer<float4> gMVOut : register(u1);
SamplerState sLin : register(s0);

static const float3 kLuma = float3(0.299, 0.587, 0.114);

// ---- luma pyramid ---------------------------------------------------------
[numthreads(8, 8, 1)]
void CSLumaL1(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= sizes.z || id.y >= sizes.w) return;
    int2 lim = int2(sizes.xy) - 1;
    int2 p = int2(id.xy) * 2;
    float3 c = gSrc.Load(int3(min(p, lim), 0)).rgb
             + gSrc.Load(int3(min(p + int2(1, 0), lim), 0)).rgb
             + gSrc.Load(int3(min(p + int2(0, 1), lim), 0)).rgb
             + gSrc.Load(int3(min(p + int2(1, 1), lim), 0)).rgb;
    gLumaOut[id.xy] = dot(c * 0.25, kLuma);
}

[numthreads(8, 8, 1)]
void CSLumaL2(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= sizes2.x || id.y >= sizes2.y) return;
    int2 lim = int2(sizes.zw) - 1;
    int2 p = int2(id.xy) * 2;
    float v = gLumaIn.Load(int3(min(p, lim), 0))
            + gLumaIn.Load(int3(min(p + int2(1, 0), lim), 0))
            + gLumaIn.Load(int3(min(p + int2(0, 1), lim), 0))
            + gLumaIn.Load(int3(min(p + int2(1, 1), lim), 0));
    gLumaOut[id.xy] = v * 0.25;
}

groupshared uint gsBest;
groupshared float gsZero;

// ---- coarse search: 8x8 blocks on 1/4-res luma, +-7 px full search ---------
// d is chosen so that cur(p) ~ prev(p + d)  (object moved from p+d to p).
[numthreads(15, 15, 1)]
void CSMotionCoarse(uint3 gid : SV_GroupID, uint3 tid : SV_GroupThreadID, uint gi : SV_GroupIndex)
{
    if (gi == 0) { gsBest = 0xFFFFFFFF; gsZero = 0; }
    GroupMemoryBarrierWithGroupSync();

    int2 d = int2(tid.xy) - 7;
    int2 b = int2(gid.xy) * 8;
    int2 lim = int2(sizes2.xy) - 1;
    float sad = 0;
    [loop] for (int y = 0; y < 8; ++y)
    {
        [unroll] for (int x = 0; x < 8; ++x)
        {
            int2 p = b + int2(x, y);
            float a = gL2Cur.Load(int3(min(p, lim), 0));
            float c = gL2Prev.Load(int3(clamp(p + d, int2(0, 0), lim), 0));
            sad += abs(a - c);
        }
    }
    if (d.x == 0 && d.y == 0) gsZero = sad;
    float pen = 0.002 * (abs(d.x) + abs(d.y));   // prefer small motion on ties / flat areas
    uint key = (((uint)((sad + pen) * 4096.0)) << 8) | gi;
    InterlockedMin(gsBest, key);
    GroupMemoryBarrierWithGroupSync();

    if (key == gsBest)
        gMVOut[gid.y * sizes2.z + gid.x] = float4(d, sad / 64.0, gsZero / 64.0);
}

// ---- refinement: 8x8 blocks on 1/2-res luma around the coarse prediction ---
[numthreads(7, 7, 1)]
void CSMotionRefine(uint3 gid : SV_GroupID, uint3 tid : SV_GroupThreadID, uint gi : SV_GroupIndex)
{
    if (gi == 0) gsBest = 0xFFFFFFFF;
    GroupMemoryBarrierWithGroupSync();

    int2 cg = min(int2(gid.xy) / 2, int2(sizes2.zw) - 1);
    float4 c = gCoarse[cg.y * sizes2.z + cg.x];
    int2 base = int2(round(c.xy * 2.0));
    int2 o = int2(tid.xy) - 3;
    int R = (int)sizes3.z;
    bool active = (abs(o.x) <= R) && (abs(o.y) <= R);
    uint key = 0xFFFFFFFF;
    float sad = 0;
    if (active)
    {
        int2 d = base + o;
        int2 b = int2(gid.xy) * 8;
        int2 lim = int2(sizes.zw) - 1;
        [loop] for (int y = 0; y < 8; ++y)
        {
            [unroll] for (int x = 0; x < 8; ++x)
            {
                int2 p = b + int2(x, y);
                float a = gL1Cur.Load(int3(min(p, lim), 0));
                float v = gL1Prev.Load(int3(clamp(p + d, int2(0, 0), lim), 0));
                sad += abs(a - v);
            }
        }
        float pen = 0.002 * (abs(o.x) + abs(o.y));
        key = (((uint)((sad + pen) * 4096.0)) << 8) | gi;
        InterlockedMin(gsBest, key);
    }
    GroupMemoryBarrierWithGroupSync();

    if (active && key == gsBest)
        gMVOut[gid.y * sizes3.x + gid.x] = float4(base + o, sad / 64.0, 0);
}

// ---- vector median over 3x3 neighbourhood (removes outlier vectors) --------
[numthreads(8, 8, 1)]
void CSMedian(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= sizes3.x || id.y >= sizes3.y) return;
    int2 g = int2(sizes3.xy);
    float4 v[9];
    [unroll] for (int j = 0; j < 3; ++j)
    {
        [unroll] for (int i = 0; i < 3; ++i)
        {
            int2 q = clamp(int2(id.xy) + int2(i - 1, j - 1), int2(0, 0), g - 1);
            v[j * 3 + i] = gMVIn[q.y * g.x + q.x];
        }
    }
    int best = 4;
    float bestS = 1e9;
    [unroll] for (int a = 0; a < 9; ++a)
    {
        float s = 0;
        [unroll] for (int b = 0; b < 9; ++b)
            s += abs(v[a].x - v[b].x) + abs(v[a].y - v[b].y);
        if (s < bestS) { bestS = s; best = a; }
    }
    float4 r = v[0];
    [unroll] for (int k = 1; k < 9; ++k) if (k == best) r = v[k];
    gMVOut[id.y * g.x + id.x] = r;
}

// ---- fullscreen triangle + interpolation -----------------------------------
struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };

VSOut VSMain(uint id : SV_VertexID)
{
    VSOut o;
    float2 uv = float2((id << 1) & 2, id & 2);
    o.uv = uv;
    o.pos = float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);
    return o;
}

float4 PSCopy(VSOut i) : SV_Target
{
    return float4(gColCur.SampleLevel(sLin, i.uv, 0).rgb, 1);
}

float4 SampleMV(float2 uv)
{
    int2 g = int2(sizes3.xy);
    float2 fp = uv * float2(sizes.xy) / 16.0 - 0.5;   // one fine cell = 16 source px
    int2 i0 = int2(floor(fp));
    float2 f = fp - float2(i0);
    int2 a = clamp(i0, int2(0, 0), g - 1);
    int2 b = clamp(i0 + 1, int2(0, 0), g - 1);
    float4 m00 = gMV[a.y * g.x + a.x];
    float4 m10 = gMV[a.y * g.x + b.x];
    float4 m01 = gMV[b.y * g.x + a.x];
    float4 m11 = gMV[b.y * g.x + b.x];
    return lerp(lerp(m00, m10, f.x), lerp(m01, m11, f.x), f.y);
}

float4 PSInterp(VSOut i) : SV_Target
{
    float t = params.x;
    float2 duv = float2(0, 0);
    if (sizes3.w != 0)
    {
        float4 m = SampleMV(i.uv);
        float conf = saturate(1.0 - max(m.z - 0.04, 0.0) / params.y);   // m.z = mean abs luma error
        duv = m.xy * 2.0 / float2(sizes.xy) * conf;                     // L1 px -> uv
    }
    float3 P = gColPrev.SampleLevel(sLin, i.uv + t * duv, 0).rgb;
    float3 C = gColCur.SampleLevel(sLin, i.uv - (1.0 - t) * duv, 0).rgb;
    float3 mc = lerp(P, C, t);
    // If the two motion-compensated samples disagree (occlusion / bad vector),
    // fall back to the temporally nearer frame instead of a ghosted blend.
    float k = smoothstep(0.15, 0.45, length(P - C));
    float3 nearest = (t < 0.5) ? P : C;
    return float4(lerp(mc, nearest, k), 1);
}
)HLSL";

} // namespace nf
