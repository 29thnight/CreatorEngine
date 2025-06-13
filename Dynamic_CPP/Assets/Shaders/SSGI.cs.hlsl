#include "Sampler.hlsli"
#define Nd 4       // 방향 개수
#define Ns 4       // 각 방향당 샘플 개수
#define Nb 32      // 비트마스크 크기
#define PI 3.14159265
static const uint sectorCount = 32u;

Texture2D<float> gDepthTex : register(t0);
Texture2D<float4> gColor : register(t1);
Texture2D<float4> gNormalTex : register(t2);
Texture2D<float4> gLightEmissive : register(t3);

RWTexture2D<float4> gOutput : register(u0);

cbuffer SSGIParams : register(b0)
{
    float4x4 invVP;
    float4x4 projection;
    float4x4 inverseProjection;
    float2 screenSize; // 화면 크기
    float radius; // 샘플링 반경
    float thickness; // 두께
    uint frameIndex;
};
float3 TangentSpacePos(float3 p, float3 np)
{
    float3 eyeVec = normalize(p);
    float3 upVec = abs(np.z) < 0.999 ? float3(0, 0, 1) : float3(0, 1, 0);
    float3 tangent = normalize(cross(upVec, np));
    float3 bitangent = -cross(np, tangent);
    return float3(dot(p, tangent), dot(p, bitangent), dot(p, eyeVec));
}
float3 CalculateWorldFromDepth(float depth, float2 texCoord)
{
    // clip space between [-1, 1]
    // flip y so that +ve y is upwards
    float2 clipXY = float4(texCoord * float2(2, -2) + float2(-1, 1), depth, 1);

    // NOTE: depth is not linearized
    // Also in range [0, 1] due to DirectX Convention
    float4 clipSpace = float4(clipXY, depth, 1);
    float4 worldSpace = mul(invVP, clipSpace);
    worldSpace /= worldSpace.w;
    return worldSpace.xyz;
}
float3 CalculateViewSpaceFromDepth(float depth, float2 texCoord)
{
    // clip space between [-1, 1]
    // flip y so that +ve y is upwards
    //float2 clipXY = texCoord * 2.0 - 1.0;
    //clipXY.y = -clipXY.y;
    float4 cliptXY = float4(texCoord * float2(2, -2) + float2(-1, 1), depth, 1);
    // NOTE: depth is not linearized
    // Also in range [0, 1] due to DirectX Convention
    float4 clipSpace = cliptXY; //float4(clipXY, depth, 1);
    float4 viewSpace = mul(inverseProjection, clipSpace);
    viewSpace.xyz /= viewSpace.w;
    return viewSpace.xyz;
}

float3 CalculateViewSpaceFromWorldNormal(float3 normal)
{
    return mul((float3x3) inverseProjection, normal);
}
float2 GetDirection(uint i, float2 noise)
{
    float angle = (i + noise.x) * (2.0 * PI) / Nd;
    return float2(cos(angle), sin(angle));
}
float randf(int x, int y)
{
    return fmod(52.9829189 * fmod(0.06711056 * (float) x + 0.00583715 * (float) y, 1.0), 1.0);
}
uint updateSectors(float minHorizon, float maxHorizon, uint outBitfield)
{
    uint startBit = (uint) (minHorizon * (float) sectorCount);
    uint horizonAngle = (uint) ceil((maxHorizon - minHorizon) * (float) sectorCount);
    uint angleBit = (horizonAngle > 0u) ? (0xFFFFFFFFu >> (sectorCount - horizonAngle)) : 0u;
    uint currentBitfield = angleBit << startBit;
    return outBitfield | currentBitfield;
}

[numthreads(16, 16, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    //float2 uv = float2(DTid.xy) / screenSize;
    //float depth = gDepthTex.SampleLevel(PointSampler, uv, 0);
    //if (depth >= 1)
    //    gOutput[DTid.xy] = float4(1.0, 0, 0, 1.0);
    
    //float3 p = CalculateViewSpaceFromDepth(depth, uv);
    //float3 np = gNormalTex.SampleLevel(PointSampler, uv, 0).rgb;
    //np = normalize(np * 2.0 - 1.0);
    ////p = TangentSpacePos(p, np);
    
    //float AO = 0;
    //float3 GI = 0;
    //for (int i = 0; i < Nd; ++i)
    //{
    //    float2 noise = frac(sin(dot(uv, uint2(12.9898, 78.233))) * float2(43758.5453, 12345.6789));
    //    float2 dir = GetDirection(i, noise);
    //    //float angle = (frac(sin(dot(uv, float2(12.9898, 78.233))) * 43758.5453)) * (2 * PI / Nd);
    //    //float2 dir = float2(cos(angle), sin(angle));

    //    uint bitmask = 0;

    //    for (int j = 0; j < Ns; ++j)
    //    {
    //        float pixelPerUnit = 1080.0 / (2.0f * tan(60 * 0.5f));
    //        float r = radius / depth * pixelPerUnit / 100.0;
    //        float step = r * ((j + 1.0) / (Ns + 1.0));
    //        float2 offsetUV = uv + dir * step;
    //        float sDepth = gDepthTex.SampleLevel(PointSampler, offsetUV, 0);
    //        float3 sf = CalculateViewSpaceFromDepth(sDepth, offsetUV); //SamplePosition(offsetUV);
    //        float3 nj = normalize(gNormalTex.SampleLevel(PointSampler, offsetUV, 0).rgb * 2.0 - 1.0);
    //        //sf = TangentSpacePos(sf, nj);
            
            
    //        float3 sb = sf - (thickness * p / length(p));
            
    //        float theta_f = atan2(sf.y, sf.x);
    //        float theta_b = atan2(sb.y, sb.x);
    //        float thetaMin = min(theta_f, theta_b);
    //        float thetaMax = max(theta_f, theta_b);
    //        uint a = uint(floor((thetaMin + PI / 2) / PI * Nb));
    //        uint b = uint(ceil((thetaMax - thetaMin + PI / 2) / PI * Nb));
    //        uint bj = ((1u << b) - 1u) ^ ((1u << a) - 1u);
            
    //        float3 cj = gLightEmissive.SampleLevel(PointSampler, offsetUV, 0).xyz;
    //        float3 lj = normalize(sf - p);
    //        uint newBits = bj & (~bitmask);
    //        uint bitCount = countbits(newBits);
    //        // Thickness check
    //        //if (abs(dot(delta, np)) > thickness)
    //        //    continue;
            
    //        //float3 nj = normalize(gNormalTex.Sample(PointSampler, offsetUV).rgb * 2.0 - 1.0);

    //        //float theta = atan2(delta.y, delta.x);
    //        //if (theta < 0)
    //        //    theta += 2 * PI;

    //        //int bitIndex = int((theta / (2 * PI)) * Nb);
    //        //uint bj = 1 << bitIndex;

    //        // GI 누적 (가리지 않은 방향만 반영)
    //        //if ((bitmask & bj) == 0)
    //        //{
    //        //    float weight = saturate(dot(np, lj)) * saturate(dot(nj, -lj));
    //        //    GI += col * weight;
    //        //}
    //        GI += (cj * bitCount / float(Nb)) * saturate(dot(np, lj)) * saturate(dot(nj, -lj)) * 100.0; /* * col * 10.0*/;
    //        bitmask |= bj; // 가렸다면 업데이트
    //    }

    //    AO += 1.0 - (countbits(bitmask) / (float) Nb);
    //}
    //float3 col = gColor.SampleLevel(PointSampler, uv, 0).rgb;

    //AO /= Nd;
    //GI /= Nd;
    
    //gOutput[DTid.xy] = float4(float3(AO, AO, AO), 1);
    
    
    float2 uv = float2(DTid.xy) / screenSize;
    float depth = gDepthTex.SampleLevel(PointSampler, uv, 0);
    uint indirect = 0u;
    uint occlusion = 0u;

    float visibility = 0.0;
    float3 lighting = float3(0.0, 0.0, 0.0);
    float2 frontBackHorizon = float2(0.0, 0.0);
    float2 aspect = screenSize.yx / screenSize.x;

    float3 position = CalculateViewSpaceFromDepth(depth, uv); //screenPosition.Sample(samplerLinear, fragUV).rgb;
    float3 camera = normalize(-position);
    float3 normal = normalize(gNormalTex.SampleLevel(PointSampler, uv, 0).rgb * 2 - 1);

    float sliceRotation = PI / (Nd - 1.0);
    float sampleScale = (-radius * projection[0][0]) / position.z;
    float sampleOffset = 0.01;
    float jitter = randf(DTid.x, DTid.y) - 0.5;

    for (float slice = 0.0; slice < Nd + 0.5; slice += 1.0)
    {
        float phi = sliceRotation * (slice + jitter) + PI;
        float2 omega = float2(cos(phi), sin(phi));
        float3 direction = float3(omega.x, omega.y, 0.0);
        float3 orthoDirection = direction - dot(direction, camera) * camera;
        float3 axis = cross(direction, camera);
        float3 projNormal = normal - axis * dot(normal, axis);
        float projLength = length(projNormal);

        float signN = sin(dot(orthoDirection, projNormal));
        float cosN = clamp(dot(projNormal, camera) / projLength, 0.0, 1.0);
        float n = signN * acos(cosN);

        for (float currentSample = 0.0; currentSample < Ns + 0.5; currentSample += 1.0)
        {
            float sampleStep = (currentSample + jitter) / Ns + sampleOffset;
            float2 sampleUV = uv - sampleStep * sampleScale * omega * aspect;
            float3 samplePosition = CalculateViewSpaceFromDepth(gDepthTex.SampleLevel(LinearSampler, sampleUV, 0), sampleUV); //screenPosition.Sample(samplerLinear, sampleUV).rgb;
            float3 sampleNormal = normalize(gNormalTex.SampleLevel(LinearSampler, sampleUV, 0).rgb * 2 - 1);
            float3 sampleLight = gLightEmissive.SampleLevel(LinearSampler, sampleUV, 0).rgb; //screenLight.Sample(samplerLinear, sampleUV).rgb;
            float3 sampleDistance = samplePosition - position;
            float sampleLength = length(sampleDistance);
            float3 sampleHorizon = sampleDistance / sampleLength;

            frontBackHorizon.x = dot(sampleHorizon, camera);
            frontBackHorizon.y = dot(normalize(sampleDistance - camera * thickness), camera);

            frontBackHorizon = acos(frontBackHorizon);
            frontBackHorizon = clamp((frontBackHorizon + n + (PI * 0.5)) / PI, 0.0, 1.0);

            indirect = updateSectors(frontBackHorizon.x, frontBackHorizon.y, 0u);
            lighting += (1.0 - (float(countbits(indirect & ~occlusion)) / sectorCount)) *
                sampleLight * saturate(dot(normal, sampleHorizon)) *
                saturate(dot(sampleNormal, -sampleHorizon));
            occlusion |= indirect;
        }

        visibility += 1.0 - (float(countbits(occlusion)) / sectorCount);
    }

    visibility /= Nd;
    lighting /= Nd;
    float3 col = gColor.SampleLevel(PointSampler, uv, 0).rgb;
    float3 lm = gLightEmissive.SampleLevel(PointSampler, uv, 0).rgb;

    gOutput[DTid.xy] = float4((col + lighting) * visibility, 1);
}