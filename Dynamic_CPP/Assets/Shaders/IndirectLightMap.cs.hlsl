#include "Sampler.hlsli"
// === 구조체 ===
struct Triangle
{
    float3 v0, v1, v2;
    float3 n0, n1, n2;
};

// === 버퍼 ===
StructuredBuffer<Triangle> g_Triangles : register(t0);
Texture2D g_PositionMap : register(t1);
Texture2D g_NormalMap : register(t2);
RWTexture2D<float4> g_IndirectLightMap : register(u0);

// === 상수 버퍼 ===
cbuffer CB_Lightmap : register(b0)
{
    uint g_TriangleCount;
    uint g_SampleCount;
    uint2 Resolution;
}

// === 유틸리티 ===
float RadicalInverse_VdC(uint bits)
{
    bits = (bits << 16) | (bits >> 16);
    bits = ((bits & 0x55555555u) << 1) | ((bits & 0xAAAAAAAAu) >> 1);
    bits = ((bits & 0x33333333u) << 2) | ((bits & 0xCCCCCCCCu) >> 2);
    bits = ((bits & 0x0F0F0F0Fu) << 4) | ((bits & 0xF0F0F0F0u) >> 4);
    bits = ((bits & 0x00FF00FFu) << 8) | ((bits & 0xFF00FF00u) >> 8);
    return float(bits) * 2.3283064365386963e-10;
}

float2 HammersleySample(uint i, uint N)
{
    return float2(float(i) / float(N), RadicalInverse_VdC(i));
}

float3 SampleHemisphere(float2 xi)
{
    float phi = 2.0 * 3.14159265 * xi.x;
    float cosTheta = 1.0 - xi.y;
    float sinTheta = sqrt(1.0 - cosTheta * cosTheta);
    return float3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
}

float3x3 BuildTBN(float3 n)
{
    float3 up = abs(n.z) < 0.99 ? float3(0, 0, 1) : float3(1, 0, 0);
    float3 t = normalize(cross(up, n));
    float3 b = cross(n, t);
    return float3x3(t, b, n);
}

bool RayTriangleIntersect(float3 orig, float3 dir, Triangle tri, out float t, out float3 bary)
{
    float3 v0 = tri.v0, v1 = tri.v1, v2 = tri.v2;
    float3 edge1 = v1 - v0;
    float3 edge2 = v2 - v0;
    float3 pvec = cross(dir, edge2);
    float det = dot(edge1, pvec);
    if (abs(det) < 1e-6)
        return false;

    float invDet = 1.0 / det;
    float3 tvec = orig - v0;
    float u = dot(tvec, pvec) * invDet;
    if (u < 0 || u > 1)
        return false;

    float3 qvec = cross(tvec, edge1);
    float v = dot(dir, qvec) * invDet;
    if (v < 0 || u + v > 1)
        return false;

    t = dot(edge2, qvec) * invDet;
    if (t < 0.0001)
        return false;

    bary = float3(1 - u - v, u, v);
    return true;
}

// === 메인 커널 ===
[numthreads(32, 32, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    if (dispatchThreadID.x >= Resolution.x || dispatchThreadID.y >= Resolution.y)
        return;

    float2 uv = (dispatchThreadID.xy + 0.5) / Resolution;
    float3 pos = g_PositionMap.SampleLevel(LinearSampler, uv, 0).rgb;
    float3 normal = normalize(g_NormalMap.SampleLevel(LinearSampler, uv, 0).rgb);

    float3x3 TBN = BuildTBN(normal);
    float3 indirect = float3(0, 0, 0);

    for (uint i = 0; i < g_SampleCount; ++i)
    {
        float2 xi = HammersleySample(i, g_SampleCount);
        float3 localDir = SampleHemisphere(xi);
        float3 worldDir = mul(localDir, TBN);

        float minT = 1e20;
        float3 color = float3(0, 0, 0);

        for (uint t = 0; t < g_TriangleCount; ++t)
        {
            float hitT;
            float3 bary;
            if (RayTriangleIntersect(pos, worldDir, g_Triangles[t], hitT, bary))
            {
                if (hitT < minT)
                {
                    minT = hitT;
                    float3 n0 = g_Triangles[t].n0;
                    float3 n1 = g_Triangles[t].n1;
                    float3 n2 = g_Triangles[t].n2;
                    float3 interpNormal = normalize(bary.x * n0 + bary.y * n1 + bary.z * n2);

                    // 단순 노멀 컬러 (디버그용)
                    color = interpNormal * 0.5 + 0.5;
                }
            }
        }

        indirect += color;
    }

    indirect /= g_SampleCount;
    g_IndirectLightMap[dispatchThreadID.xy] = float4(indirect, 1);
}