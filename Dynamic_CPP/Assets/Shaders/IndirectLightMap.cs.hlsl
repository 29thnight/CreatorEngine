#include "Sampler.hlsli"
// 라이트맵처럼 라이트맵 텍스쳐의 한 픽셀을 선택하여
// 그 픽셀의 pos, normal을 가져와서 ray를 삼각형에 쏘고
// 맞은 삼각형이 있으면 샘플링해서 metalic, roughness로 계산된 광원을 가져온다.


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
    float3 boundsMax;
    int left; // child index
    int right; // child index
    int start; // index range for triangles
    int end;
    bool isLeaf;
    int pad;
};

struct Ray
{
    float3 origin;
    float3 dir;
};

// === 버퍼 ===
Texture2DArray<float4> lightMap : register(t1);
Texture2D<float4> this_PositionMap : register(t2);
Texture2D<float4> this_NormalMap : register(t3);


StructuredBuffer<Triangle> triangles : register(t10);
StructuredBuffer<int> TriIndices : register(t11); // BVH에서 삼각형 인덱스
StructuredBuffer<BVHNode> BVHNodes : register(t12);
RWTexture2D<float4> g_IndirectLightMap : register(u0);

// === 상수 버퍼 ===
cbuffer CB_Lightmap : register(b0)
{
    int2 Resolution;
    int triangleCount;    // indice
    int g_SampleCount;    // ray 샘플링 수
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
};

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

inline bool RayTriangleIntersect(float3 orig, float3 dir, Triangle tri, out float t, out float3 bary)
{
    //float3 origin = orig + dir * 0.001; // 자신을 때리는 경우 방지.
    
    float3 v0 = tri.v0, v1 = tri.v1, v2 = tri.v2;
    float3 edge1 = v1 - v0;
    float3 edge2 = v2 - v0;
    float3 pvec = cross(dir, edge2);
    float det = dot(edge1, pvec);
    if (det < 1e-4)
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
    return tEnter <= tExit && tExit > 0;
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
    float2 localUV = (targetPos - Offset) / Size;
    //if (dispatchThreadID.x >= Resolution.x || dispatchThreadID.y >= Resolution.y)
    //    return;

    //float2 uv = (dispatchThreadID.xy + 0.5) / Resolution;
    //float3 pos = Sampling(this_PositionMap, LinearSampler, localUV, float2(2048, 2048)).xyz;
    float4 pos = this_PositionMap.SampleLevel(LinearSampler, localUV, 0);
    float4 normal = normalize(this_NormalMap.SampleLevel(LinearSampler, localUV, 0));
    
    float4 worldPos = mul(worldMat, pos);
    float3 worldNor = mul(worldMat, float4(normal.xyz, 0)).xyz;

    float3x3 TBN = BuildTBN(normalize(worldNor));
    float3 indirect = float3(0, 0, 0);
    
    float2 xi;
    float3 localDir;
    float3 worldDir;

    float minT;// = 1e20;
    float3 color;// = float3(0, 0, 0);
                //g_IndirectLightMap[DTid.xy] = float4(0, 1, 0, 1);
        
    float hitT;// = 0;
    float3 bary;// = { 0, 0, 0 };
    
    //for (uint i = 0; i < 512; ++i)
    //{
    //     xi = HammersleySample(i, 512);
    //     localDir = SampleHemisphere(xi);
    //     worldDir = mul(localDir, TBN);

    //    minT = 1e20;
    //    color = float3(0, 0, 0);
    //      //g_IndirectLightMap[DTid.xy] = float4(0, 1, 0, 1);
        
    //    hitT = 0;
    //    bary = float3(0, 0, 0);
    //    [fastopt]
    //    for (int t = triangleCount - 1; t >= 0; --t)
    //    {
    //        if (RayTriangleIntersect(worldPos.xyz, worldDir, triangles[t], hitT, bary))
    //        {
    //            if (hitT < minT)
    //            {
    //                minT = hitT;
    //                //float3 n0 = triangles[t].n0;
    //                //float3 n1 = triangles[t].n1;
    //                //float3 n2 = triangles[t].n2;
    //                //float3 interpNormal = normalize(bary.x * n0 + bary.y * n1 + bary.z * n2);

    //                float2 interpUV = bary.x * triangles[t].lightmapUV0 + bary.y * triangles[t].lightmapUV1 + bary.z * triangles[t].lightmapUV2;
                    
    //                color = lightMap.SampleLevel(LinearSampler, float3(interpUV, triangles[t].lightmapIndex), 0.0).rgb;
    //                // 단순 노멀 컬러 (디버그용)
    //                //color = interpNormal;
    //            }
    //        }
    //    }

    //    indirect += color;
    //}
    for (int i = 0; i < 256; ++i){
        float2 xi = HammersleySample(i, 256);
        float3 localDir = SampleHemisphere(xi);
        float3 worldDir = mul(localDir, TBN);

        float minT = 1e20;
        float3 finalColor = float3(0, 0, 0);
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
                for (int j = node.start; j < node.end; ++j)
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
                        }
                    }
                }
            }
            else
            {
                stack[stackPtr++] = node.left;
                stack[stackPtr++] = node.right;
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

            finalColor = lightMap.SampleLevel(LinearSampler, float3(interpUV, hitTri.lightmapIndex), 0.0).rgb;
        }

        indirect += finalColor;
    }

    GroupMemoryBarrierWithGroupSync();
    indirect /= 256;
    g_IndirectLightMap[DTid.xy] = float4(indirect, 1);
}

/*
1. lightmap에 기록된 광량을 사용해서 0번째 픽셀의 this_PositionMap, this_NormalMap을 바탕으로
   ray를 반구방향으로 g_SampleCount만큼 쏴서 샘플링한 값을 저장.

*/