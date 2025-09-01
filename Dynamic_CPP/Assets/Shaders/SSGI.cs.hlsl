#include "Sampler.hlsli"
#include "BRDF.hlsli"
#define Nd 4       // 방향 개수
#define Ns 4       // 각 방향당 샘플 개수
#define Nb 32      // 비트마스크 크기
#define PI 3.14159265
#define twoPI PI * 2
#define halfPI PI * 0.5
static const uint sectorCount = 32u;

Texture2D<float> gDepthTex : register(t0);
Texture2D<float4> gColor : register(t1);
Texture2D<float4> gNormalTex : register(t2);
Texture2D<float4> gLightEmissive : register(t3);
Texture2D<float4> MetalRough : register(t4);

RWTexture2D<float4> gOutput : register(u0);

//uint2 twobytwoSample[4] =
//{
//    { 0, 0 },
//    { 0, 1 },
//    { 1, 0 },
//    { 1, 1 }
//};

uint2 twobytwoSample[16] =
{
    {0,0 },{0,1 },{0,2 },{0,3 },
    {1,0 },{1,1 },{1,2 },{1,3 },
    {2,0 },{2,1 },{2,2 },{2,3 },
    {3,0 },{3,1 },{3,2 },{3,3 }
};

cbuffer SSGIParams : register(b0)
{
    float4x4 viewMat;
    float4x4 projection;
    float4x4 inverseProjection;
    float2 screenSize; // 화면 크기
    float radius; // 샘플링 반경
    float thickness; // 두께
    uint frameIndex;
    int ratio;
    float intensity;
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
    float3 viewNormal = mul((float3x3)viewMat, normal);
    return normalize(viewNormal);
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
    //uint2 twobytwo = twobytwoSample[frameIndex % (ratio * ratio)];
    float2 invScreenSize = 1.0 / screenSize;
    
    // DTid.xy = screenSize / 2 = (960, 540)
    float2 uv = float2(DTid.xy * ratio ) * invScreenSize;
    //float2 uv = float2(DTid.xy) / screenSize;
    //uv *= 2.0;
    float depth = gDepthTex.SampleLevel(PointSampler, uv, 0);
    //depth = -linearZ(depth); // linearize depth
    uint indirect = 0u;
    uint occlusion = 0u;

    float visibility = 0.0;
    float3 lighting = float3(0.0, 0.0, 0.0);
    float2 frontBackHorizon = float2(0.0, 0.0);
    float2 aspect = screenSize.yx / screenSize.x;

    float3 position = CalculateViewSpaceFromDepth(depth, uv); //screenPosition.Sample(samplerLinear, fragUV).rgb;
    float3 camera = normalize(-position);
    float3 normal = CalculateViewSpaceFromWorldNormal((gNormalTex.SampleLevel(PointSampler, uv, 0).rgb * 2 - 1));

    float sliceRotation = twoPI / (Nd - 1.0);
    float sampleScale = (-radius * projection[0][0]) / position.z;
    float sampleOffset = 0.01;
    float jitter = randf(DTid.x, DTid.y) - 0.5;

    float3 albedo = gColor.SampleLevel(LinearSampler, uv, 0);
    float2 metalrough = MetalRough.SampleLevel(LinearSampler, uv, 0).rg;
    float metallic = metalrough.x;
    float roughness = metalrough.y;
    float3 F0 = float3(0.04, 0.04, 0.04);
    F0 = lerp(F0, albedo, metallic);
    
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
            float sDepth = gDepthTex.SampleLevel(LinearSampler, sampleUV, 0);
            //sDepth = -linearZ(sDepth); // linearize depth
            float3 samplePosition = CalculateViewSpaceFromDepth(sDepth, sampleUV); //screenPosition.Sample(samplerLinear, sampleUV).rgb;
            float3 sampleNormal = CalculateViewSpaceFromWorldNormal((gNormalTex.SampleLevel(LinearSampler, sampleUV, 0).rgb * 2 - 1));
            float3 sampleLight = gLightEmissive.SampleLevel(LinearSampler, sampleUV, 0).rgb; //screenLight.Sample(samplerLinear, sampleUV).rgb;
            float3 sampleDistance = samplePosition - position;
            float sampleLength = length(sampleDistance);
            float3 sampleHorizon = sampleDistance / sampleLength;
            float3 L = normalize(sampleDistance);
            float3 H = normalize(camera + L);
            
            frontBackHorizon.x = dot(sampleHorizon, camera);
            frontBackHorizon.y = dot(normalize(sampleDistance - camera * thickness), camera);

            frontBackHorizon = acos(frontBackHorizon);
            frontBackHorizon = clamp((frontBackHorizon + n + halfPI) / PI, 0.0, 1.0);

            indirect = updateSectors(frontBackHorizon.x, frontBackHorizon.y, 0u);
            if (dot(normal, L) > 0)
            {
                float NDF = DistributionGGX(dot(normal, H), roughness);
                float G = GeometrySmith(max(dot(normal, camera), 0.0), max(dot(normal, L), 0.0), roughness);
                float3 F = fresnelSchlick(max(dot(H, camera), 0.0), F0);
                float3 kS = F;
                float3 kD = float3(1, 1, 1) - kS;
                kD *= 1.0 - metallic;
                float NdotL = max(dot(normal, L), 0.0);
                float3 numerator = NDF * G * F;
                float denominator = 4.0 * max(dot(normal, camera), 0.0) * NdotL + 0.001;
                float3 specular = numerator / denominator;
                
                lighting += (1.0 - float(countbits(indirect & ~occlusion)) / float(sectorCount)) *
                (kD * albedo / PI + specular) * NdotL * sampleLight;
                //sampleLight * saturate(dot(normal, sampleHorizon)) *
                //saturate(dot(sampleNormal, -sampleHorizon));
            }
            occlusion |= indirect;
        }

        visibility += 1.0 - (float(countbits(occlusion)) / float(sectorCount));
    }

    visibility /= Nd;
    lighting /= Nd * Ns;
    
    gOutput[DTid.xy] = float4(lighting * intensity, visibility);
}