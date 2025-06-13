#include "Sampler.hlsli"
#define Nd 8       // 방향 개수
#define Ns 4       // 각 방향당 샘플 개수
#define Nb 32      // 비트마스크 크기
#define PI 3.14159265

Texture2D<float> gDepthTex : register(t0);
Texture2D<float4> gColor : register(t1);
Texture2D<float4> gNormalTex : register(t2);
Texture2D<float4> gLightEmissive : register(t3);

RWTexture2D<float4> gOutput : register(u0);

cbuffer SSGIParams : register(b0)
{
    float4x4 inverseProjection;
    float2 screenSize; // 화면 크기
    float radius; // 샘플링 반경
    float thickness; // 두께
};
float3 TangentSpacePos(float3 p, float3 np)
{
    float3 eyeVec = normalize(p);
    float3 upVec = abs(np.z) < 0.999 ? float3(0, 0, 1) : float3(0, 1, 0);
    float3 tangent = normalize(cross(upVec, np));
    float3 bitangent = -cross(np, tangent);
    return float3(dot(p, tangent), dot(p, bitangent), dot(p, eyeVec));
}
float3 CalculateViewSpaceFromDepth(float depth, float2 texCoord)
{
    // clip space between [-1, 1]
    // flip y so that +ve y is upwards
    float2 clipXY = texCoord * 2.0 - 1.0;
    clipXY.y = -clipXY.y;

    // NOTE: depth is not linearized
    // Also in range [0, 1] due to DirectX Convention
    float4 clipSpace = float4(clipXY, depth, 1);
    float4 viewSpace = mul(inverseProjection, clipSpace);
    //viewSpace /= viewSpace.w;
    return viewSpace.xyz;
}
float2 GetDirection(uint i, float2 noise)
{
    float angle = (i + noise.x) * (2.0 * PI) / Nd;
    return float2(cos(angle), sin(angle));
}
[numthreads(16, 16, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    float2 uv = float2(DTid.xy) / screenSize;
    float depth = gDepthTex.SampleLevel(PointSampler, uv, 0);
    if (depth >= 1)
        gOutput[DTid.xy] = float4(1.0, 0, 0, 1.0);
    
    float3 p = CalculateViewSpaceFromDepth(depth, uv);
    float3 np = gNormalTex.SampleLevel(PointSampler, uv, 0).rgb;
    np = normalize(np * 2.0 - 1.0);
    p = TangentSpacePos(p, np);
    
    float AO = 0;
    float3 GI = 0;
    for (int i = 0; i < Nd; ++i)
    {
        float2 noise = frac(sin(dot(uv, uint2(12.9898, 78.233))) * float2(43758.5453, 12345.6789));
        float2 dir = GetDirection(i, noise);
        //float angle = (frac(sin(dot(uv, float2(12.9898, 78.233))) * 43758.5453)) * (2 * PI / Nd);
        //float2 dir = float2(cos(angle), sin(angle));

        uint bitmask = 0;

        for (int j = 0; j < Ns; ++j)
        {
            float pixelPerUnit = 1080.0 / (2.0f * tan(60 * 0.5f));
            float r = radius / depth * pixelPerUnit / 100.0;
            float step = r * ((j + 1.0) / (Ns + 1.0));
            float2 offsetUV = uv + dir * step;
            float sDepth = gDepthTex.SampleLevel(PointSampler, offsetUV, 0);
            float3 sf = CalculateViewSpaceFromDepth(sDepth, offsetUV); //SamplePosition(offsetUV);
            float3 nj = normalize(gNormalTex.SampleLevel(PointSampler, offsetUV, 0).rgb * 2.0 - 1.0);
            sf = TangentSpacePos(sf, nj);
            
            
            float3 sb = sf - (thickness * p / length(p));
            
            float theta_f = atan2(sf.y, sf.x);
            float theta_b = atan2(sb.y, sb.x);
            float thetaMin = min(theta_f, theta_b);
            float thetaMax = max(theta_f, theta_b);
            uint a = uint(floor((thetaMin + PI / 2) / PI * Nb));
            uint b = uint(ceil((thetaMax - thetaMin + PI / 2) / PI * Nb));
            uint bj = ((1u << b) - 1u) ^ ((1u << a) - 1u);
            
            float3 cj = gLightEmissive.SampleLevel(PointSampler, offsetUV, 0).xyz;
            float3 lj = normalize(sf - p);
            uint newBits = bj & (~bitmask);
            uint bitCount = countbits(newBits);
            // Thickness check
            //if (abs(dot(delta, np)) > thickness)
            //    continue;
            
            //float3 nj = normalize(gNormalTex.Sample(PointSampler, offsetUV).rgb * 2.0 - 1.0);

            //float theta = atan2(delta.y, delta.x);
            //if (theta < 0)
            //    theta += 2 * PI;

            //int bitIndex = int((theta / (2 * PI)) * Nb);
            //uint bj = 1 << bitIndex;

            // GI 누적 (가리지 않은 방향만 반영)
            //if ((bitmask & bj) == 0)
            //{
            //    float weight = saturate(dot(np, lj)) * saturate(dot(nj, -lj));
            //    GI += col * weight;
            //}
            GI += (cj * bitCount / float(Nb)) * saturate(dot(np, lj)) * saturate(dot(nj, -lj)) * 100.0; /* * col * 10.0*/;
            bitmask |= bj; // 가렸다면 업데이트
        }

        AO += 1.0 - (countbits(bitmask) / (float) Nb);
    }
    float3 col = gColor.SampleLevel(PointSampler, uv, 0).rgb;

    AO /= Nd;
    GI /= Nd;
    
    gOutput[DTid.xy] = float4((col + GI) * AO, 1);
}