#include "Sampler.hlsli"
#define Nd 8       // 방향 개수
#define Ns 4       // 각 방향당 샘플 개수
#define Nb 32      // 비트마스크 크기
#define PI 3.14159265

struct PixelShaderInput // see Fullscreen.vs.hlsl
{
    float4 position : SV_POSITION;
    float2 texCoord : TEXCOORD0;
};

cbuffer CBuffer : register(b3)
{
    float4x4 viewProjection;
    float4x4 inverseViewProjection;
    float4 kernel[64];
    float4 cameraPos;
    float radius;
    float thickness;
    float2 windowSize;
}

Texture2D gDepthTex : register(t0);
Texture2D gNormalTex : register(t1);
Texture2D gNoise : register(t2);
Texture2D gColor : register(t3);

float3 CalculateWorldFromDepth(float depth, float2 texCoord)
{
    // clip space between [-1, 1]
    // flip y so that +ve y is upwards
    float2 clipXY = texCoord * 2.0 - 1.0;
    clipXY.y = -clipXY.y;

    // NOTE: depth is not linearized
    // Also in range [0, 1] due to DirectX Convention
    float4 clipSpace = float4(clipXY, depth, 1);
    float4 worldSpace = mul(inverseViewProjection, clipSpace);
    worldSpace /= worldSpace.w;
    return worldSpace.xyz;
}

float4 main(PixelShaderInput IN) : SV_TARGET
{
    //float depth = gDepthTex.Sample(PointSampler, IN.texCoord).r;
    //if (depth >= 1)
    //    return float4(1.0, 1.0, 1.0, 1.0);

    //float3 posW = CalculateWorldFromDepth(depth, IN.texCoord);
    //float3 normal = gNormalTex.Sample(PointSampler, IN.texCoord).rgb;
    //normal = normalize(normal * 2.0 - 1.0);
    //float centerDepth = distance(posW, cameraPos.xyz);
    //float2 noiseScale = windowSize / 4.0;
    //float3 randDir = gNoise.Sample(PointSampler, IN.texCoord * noiseScale).rgb;

    //float3 tangent = normalize(randDir - normal * dot(randDir, normal));
    //float3 bitangent = cross(normal, tangent);
    //float3x3 tbn = transpose(float3x3(tangent, bitangent, normal));

    //float occlusion = 0.0;
    //for (int i = 0; i < 64; ++i)
    //{
    //    // find out a desired world position to sample
    //    float3 kernelPosW = mul(tbn, kernel[i].rgb);
    //    float3 samplePosW = posW + kernelPosW * radius;
    //    float sampleDepth = distance(samplePosW, cameraPos.xyz);

    //    // project it to the clip space so we know where can sample from the depth buffer
    //    float4 samplePosClip = mul(viewProjection, float4(samplePosW, 1));
    //    samplePosClip /= samplePosClip.w;

    //    // invert y and put to [0 - 1]
    //    float2 sampleUV = float2(samplePosClip.x, -samplePosClip.y) * 0.5f + 0.5f;

    //    // reject samples outside of the range
    //    if (sampleUV.x < 0 || sampleUV.x > 1 || sampleUV.y < 0 || sampleUV.y > 1)
    //    {
    //        occlusion += 0.0;
    //        continue;
    //    }

    //    // sample our scene for actual depth
    //    float depthFromTex = gDepthTex.Sample(PointSampler, sampleUV.xy).r;
    //    float3 scenePos = CalculateWorldFromDepth(depthFromTex, sampleUV.xy);
    //    float sceneDepth = distance(scenePos, cameraPos.xyz);

    //    float depthDiff = abs(sceneDepth - centerDepth);
    //    float rangeCheck = smoothstep(0.0, 1.0, radius / depthDiff);
    //    occlusion += step(sceneDepth, sampleDepth) * rangeCheck;
    //}
    //occlusion /= 64.0;
    //float factor = 1 - occlusion;
    //return float4(factor.rrr, 1);
    
    
    
    
    float2 uv = IN.texCoord;
    float depth = gDepthTex.Sample(PointSampler, IN.texCoord).r;
    if (depth >= 1)
        return float4(1.0, 1.0, 1.0, 1.0);
    
    float3 p = CalculateWorldFromDepth(depth, IN.texCoord);
    float3 normal = gNormalTex.Sample(PointSampler, IN.texCoord).rgb;
    float3 np = normalize(normal * 2.0 - 1.0);

    float AO = 0;
    float3 GI = 0;

    for (int i = 0; i < Nd; ++i)
    {
        float angle = (i + frac(sin(dot(uv, float2(12.9898, 78.233))) * 43758.5453)) * (2 * PI / Nd);
        float2 dir = float2(cos(angle), sin(angle));

        uint bitmask = 0;

        for (int j = 0; j < Ns; ++j)
        {
            float step = radius * ((j + 1.0) / (Ns + 1.0));
            float2 offsetUV = uv + dir * step;

            float3 sf = CalculateWorldFromDepth(gDepthTex.Sample(PointSampler, offsetUV).r, offsetUV); //SamplePosition(offsetUV);
            float3 sb = sf - (thickness * p / length(p));
            float3 delta = sf - p;
            
            float theta_f = atan2(sf.y, sf.x);
            float theta_b = atan2(sb.y, sb.x);
            float thetaMin = min(theta_f, theta_b);
            float thetaMax = max(theta_f, theta_b);
            int a = int(floor((thetaMin + PI / 2) / PI * Nb));
            int b = int(ceil((thetaMax - thetaMin + PI / 2) / PI * Nb));
            uint bj = ((1u << b) - 1u) ^ ((1u << a) - 1u);
            
            //float3 cj = GBuffer2.Load(int3(sampleCoord, 0)).xyz;
            float3 nj = normalize(gNormalTex.Sample(PointSampler, offsetUV).rgb * 2.0 - 1.0);
            float3 lj = normalize(sf - p);
            uint newBits = bj & (~bitmask);
            uint bitCount = countbits(newBits);
            // Thickness check
            //if (abs(dot(delta, np)) > thickness)
            //    continue;

            //float3 lj = normalize(delta);
            //float3 nj = normalize(gNormalTex.Sample(PointSampler, offsetUV).rgb * 2.0 - 1.0);
            float3 col = gColor.Sample(PointSampler, offsetUV).rgb;

            float theta = atan2(delta.y, delta.x);
            if (theta < 0)
                theta += 2 * PI;

            int bitIndex = int((theta / (2 * PI)) * Nb);
            //uint bj = 1 << bitIndex;

            // GI 누적 (가리지 않은 방향만 반영)
            //if ((bitmask & bj) == 0)
            //{
            //    float weight = saturate(dot(np, lj)) * saturate(dot(nj, -lj));
            //    GI += col * weight;
            //}
            GI += (/*cj * */bitCount / float(Nb)) * saturate(dot(np, lj)) * saturate(dot(nj, -lj));
            bitmask |= bj;
        }

        AO += 1.0 - (countbits(bitmask) / (float) Nb);
    }

    AO /= Nd;
    GI /= Nd;
    return float4(GI, AO);
}
