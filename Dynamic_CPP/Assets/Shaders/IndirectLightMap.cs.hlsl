#include "Sampler.hlsli"
// 라이트맵처럼 라이트맵 텍스쳐의 한 픽셀을 선택하여
// 그 픽셀의 pos, normal을 가져와서 ray를 삼각형에 쏘고
// 맞은 삼각형이 있으면 샘플링해서 metalic, roughness로 계산된 광원을 가져온다.
//#define Indirect_Max_Sampling_Count 64

// === 구조체 ===
struct Triangle
{
    float4 v0, v1, v2;
    float4 n0, n1, n2;
    float2 uv0, uv1, uv2;
    float2 lightmapUV0, lightmapUV1, lightmapUV2;
    int lightmapIndex;
    int3 pad;
};

struct BVHNode
{
    float3 boundsMin;
    int pad1;
    float3 boundsMax;
    int pad2;
    int left; // child index
    int right; // child index
    int start; // index range for triangles
    int end;
    bool isLeaf;
    int3 pad3;
};

struct Ray
{
    float3 origin;
    float3 dir;
};

// === 버퍼 ===
Texture2DArray<float4> lightMap : register(t0);
Texture2D<float4> this_PositionMap : register(t1);
Texture2D<float4> this_NormalMap : register(t2);


StructuredBuffer<Triangle> triangles : register(t10);
StructuredBuffer<int> TriIndices : register(t11); // BVH에서 삼각형 인덱스
StructuredBuffer<BVHNode> BVHNodes : register(t12);
RWTexture2D<float4> g_IndirectLightMap : register(u0);
RWTexture2D<float4> g_DirectionalMap : register(u1);

// === 상수 버퍼 ===
cbuffer CB_Lightmap : register(b0)
{
    int2 Resolution;
    int triangleCount;    // indice
    int sampleCount;
}
cbuffer CB : register(b1)
{
    int2 Offset; // 타겟 텍스처에서 그릴 위치
    int2 Size; // 복사할 영역 크기
    int useAO;
}
cbuffer transform : register(b2)
{
    matrix worldMat;
}
cbuffer CB_Hammersley_Sample : register(b3)
{
    float2 xi;
}

// === 유틸리티 ===
//float RadicalInverse_VdC(uint bits)
//{
//    bits = (bits << 16) | (bits >> 16);
//    bits = ((bits & 0x55555555u) << 1) | ((bits & 0xAAAAAAAAu) >> 1);
//    bits = ((bits & 0x33333333u) << 2) | ((bits & 0xCCCCCCCCu) >> 2);
//    bits = ((bits & 0x0F0F0F0Fu) << 4) | ((bits & 0xF0F0F0F0u) >> 4);
//    bits = ((bits & 0x00FF00FFu) << 8) | ((bits & 0xFF00FF00u) >> 8);
//    return float(bits) * 2.3283064365386963e-10;
//}

//float2 HammersleySample(uint i, uint N)
//{
//    return float2(float(i) / float(N), RadicalInverse_VdC(i));
//}

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

//Moller-Trumbore
inline bool RayTriangleIntersect(float3 orig, float3 dir, Triangle tri, out float t, out float3 bary)
{
    float3 origin = orig + dir * 0.001; // 자신을 때리는 경우 방지.
    
    float3 v0 = tri.v0, v1 = tri.v1, v2 = tri.v2;
    float3 edge1 = v1 - v0;
    float3 edge2 = v2 - v0;
    float3 pvec = cross(dir, edge2);
    float det = dot(edge1, pvec);
    if (abs(det) < 1e-6)
        return false;

    float invDet = 1.0 / det;
    float3 tvec = origin - v0;
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
float4 Sampling(Texture2D tex, SamplerState state, float2 uv, float2 textureSize)
{
    float2 texel = float2(1, 1) / textureSize;
    float3 color = { 0, 0, 0 };
    
    for (int x = -2; x < 3; ++x)
    {
        //[unroll]
        for (int y = -2; y < 3; ++y)
        {
            color += tex.SampleLevel(state, uv + (float2(x, y) * texel), 0).rgb;
        }
    }
    color /= 25.0;
    return float4(color, 1);
}
float4 SamplingArray(Texture2DArray tex, SamplerState state, float3 uv, float2 textureSize)
{
    float2 texel = float2(1, 1) / textureSize;
    float3 color = { 0, 0, 0 };
    
    for (int x = -2; x < 3; ++x)
    {
        //[unroll]
        for (int y = -2; y < 3; ++y)
        {
            color += tex.SampleLevel(state, uv + float3((float2(x, y) * texel), 0), 0).rgb;
        }
    }
    color /= 25.0;
    return float4(color, 1);
}

bool IntersectAABB(Ray ray, float3 bmin, float3 bmax)
{
    float3 t1 = (bmin - ray.origin) / ray.dir;
    float3 t2 = (bmax - ray.origin) / ray.dir;
    float3 tmin = min(t1, t2);
    float3 tmax = max(t1, t2);
    float tEnter = max(max(tmin.x, tmin.y), tmin.z);
    float tExit = min(min(tmax.x, tmax.y), tmax.z);
    return tEnter <= tExit && tExit > 0 && tEnter < tExit;
}

bool IntersectAABBWithT(float3 rayOrigin, float3 rayDir, float3 minB, float3 maxB, out float tNear)
{
    float3 invDir = 1.0 / rayDir;
    float3 t0 = (minB - rayOrigin) * invDir;
    float3 t1 = (maxB - rayOrigin) * invDir;

    float3 tmin = min(t0, t1);
    float3 tmax = max(t0, t1);

    float tEnter = max(max(tmin.x, tmin.y), tmin.z);
    float tExit = min(min(tmax.x, tmax.y), tmax.z);

    tNear = tEnter;
    return tExit >= tEnter && tExit > 0;
}

// sRGB/Rec.709 표준 : 명도 계산 함수
float luminance(float3 color)
{
    return dot(color, float3(0.2126, 0.7152, 0.0722));
}

// === 메인 커널 ===
[numthreads(32, 32, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    float2 targetPos = float2(DTid.xy);

    // 유효 범위 체크
    if (targetPos.x < Offset.x || targetPos.x >= (Offset.x + Size.x) ||
        targetPos.y < Offset.y || targetPos.y >= (Offset.y + Size.y))
        return;

    
    // localUV는 현재 타일(이 메쉬)의 정규화된 UV (0~1)
    float2 localUV = (targetPos + 0.5f - Offset) / Size;

    float4 pos = this_PositionMap.SampleLevel(PointSampler, localUV, 0);
    float4 normal = normalize(this_NormalMap.SampleLevel(PointSampler, localUV, 0));
    
    float4 worldPos = mul(worldMat, pos);
    float3 worldNor = mul(worldMat, float4(normal.xyz, 0)).xyz;

    //if (pos.a != 1)
    //    return;
    
    float3x3 TBN = BuildTBN(normalize(worldNor));
    float3 indirect = float3(0, 0, 0);

    
    //for (int i = 0; i < Indirect_Max_Sampling_Count; ++i)
    //{
        //float2 xi = HammersleySample(i, Indirect_Max_Sampling_Count);
    
        float3 localDir = SampleHemisphere(xi);
        float3 worldDir = mul(localDir, TBN);

        float minT = 1e19;
        float3 finalColor = float3(0, 0, 0);
        float3 flnalDirectional = float3(0, 0, 0);
        float3 hitBary = float3(0, 0, 0);
        int hitTriIndex = -1;

        // ====== BVH Traversal ======
        int stack[64];
        int stackPtr = 0;
        stack[stackPtr++] = 0; // root node index

        while (stackPtr > 0)
        {
            int nodeIndex = stack[--stackPtr];
            BVHNode node = BVHNodes[nodeIndex];
            Ray ray = { worldPos.xyz, worldDir };
            
            if (!IntersectAABB(ray, node.boundsMin, node.boundsMax))
                continue;
            
            
            if (node.isLeaf)
            {
                for (int j = node.start; j <= node.end; ++j)
                {
                    int triIndex = TriIndices[j]; // 인덱스를 통해 삼각형 참조
                    Triangle tri = triangles[triIndex];

                    float t;
                    float3 bary;
                    if (RayTriangleIntersect(worldPos.xyz, worldDir, tri, t, bary))
                    {
                        if (t < minT)
                        {
                            minT = t;
                            hitBary = bary;
                            hitTriIndex = triIndex;
                            //break;
                        }
                    }
                }
            }
            else
            {
                float tLeft = 1e20, tRight = 1e20;
                bool hitLeft = IntersectAABBWithT(ray.origin, ray.dir, BVHNodes[node.left].boundsMin, BVHNodes[node.left].boundsMax, tLeft);
                bool hitRight = IntersectAABBWithT(ray.origin, ray.dir, BVHNodes[node.right].boundsMin, BVHNodes[node.right].boundsMax, tRight);
                
                if (minT >= tLeft)
                {
                    stack[stackPtr++] = node.left;
                }
                if (minT >= tRight)
                {
                    stack[stackPtr++] = node.right;
                }
            }
        }

        // ====== Hit 처리 ======
        if (hitTriIndex >= 0)
        {
            Triangle hitTri = triangles[hitTriIndex];
            float2 interpUV =
            hitBary.x * hitTri.lightmapUV0 +
            hitBary.y * hitTri.lightmapUV1 +
            hitBary.z * hitTri.lightmapUV2;
            
            float3 hitNormal =
            hitBary.x * hitTri.n0 +
            hitBary.y * hitTri.n1 +
            hitBary.z * hitTri.n2;
            //hitNormal = normalize(hitNormal);
            
            // ray가 광원이 되는 삼각형의 노멀에 가까울 때 가장세고, 받는 삼각형의 노멀에 가까울 때 가장 많이 받음.
            float diffuseFactor = max(dot(worldNor, worldDir), 0.0f) * max(dot(hitNormal, -worldDir), 0.0f);

            finalColor = lightMap.SampleLevel(PointSampler, float3(interpUV, hitTri.lightmapIndex), 0.0).rgb;
            finalColor *= diffuseFactor;
        }
        float distance = minT; // 이미 계산된 ray hit 거리
        float attenuation = 1.0 / (distance * distance + 1.0);
        indirect += finalColor * attenuation;
    flnalDirectional = luminance(finalColor * attenuation) * -worldDir;
    //}

    //GroupMemoryBarrierWithGroupSync();
    //indirect /= sampleCount;//Indirect_Max_Sampling_Count;
    g_IndirectLightMap[DTid.xy] += float4(indirect, 1);
    g_DirectionalMap[DTid.xy] += float4(flnalDirectional, 1);
}

/*
1. lightmap에 기록된 광량을 사용해서 0번째 픽셀의 this_PositionMap, this_NormalMap을 바탕으로
   ray를 반구방향으로 g_SampleCount만큼 쏴서 샘플링한 값을 저장.

*/