#include "Sampler.hlsli"
#define Nd 8       // 방향 개수
#define Ns 4       // 각 방향당 샘플 개수
#define Nb 32      // 비트마스크 크기
#define PI 3.14159265
// visibility bitmask 360도를 비트수만큼 쪼개서 가린영역을 저장.

struct PixelShaderInput // see Fullscreen.vs.hlsl
{
    float4 position : SV_POSITION;
    float2 texCoord : TEXCOORD0;
};

cbuffer CBuffer : register(b3)
{
    float4x4 viewProjection;
    float4x4 inverseViewProjection;
    float4x4 inverseProjection;
    float4 kernel[64];
    float4 cameraPos;
    float radius;
    float thickness;
    float2 windowSize;
    uint frameIndex;
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

float3 CalculateViewSpaceFromDepth(float depth, float2 texCoord)
{
    // clip space between [-1, 1]
    // flip y so that +ve y is upwards
    float2 clipXY = texCoord;// * 2.0 - 1.0;
    clipXY.y = -clipXY.y;

    // NOTE: depth is not linearized
    // Also in range [0, 1] due to DirectX Convention
    float4 clipSpace = float4(clipXY, depth, 1);
    float4 viewSpace = mul(inverseProjection, clipSpace);
    //viewSpace /= viewSpace.w;
    return viewSpace.xyz;
}

float3 TangentSpacePos(float3 p, float3 np)
{
    float3 eyeVec = normalize(p);
    float3 upVec = abs(np.z) < 0.999 ? float3(0, 0, 1) : float3(0, 1, 0);
    float3 tangent = normalize(cross(upVec, np));
    float3 bitangent = -cross(np, tangent);
    return float3(dot(p, tangent), dot(p, bitangent), dot(p, eyeVec));
}
float3 TangentSpacePos(float3x3 tbn, float3 p)
{
    return mul(tbn, p);
}
float3 TangentSpaceNorm(float3 p, float3 np)
{
    float3 eyeVec = normalize(p);
    float3 upVec = abs(np.z) < 0.999 ? float3(0, 0, 1) : float3(0, 1, 0);
    float3 tangent = normalize(cross(upVec, np));
    float3 bitangent = -cross(np, tangent);
    return float3(dot(np, tangent), dot(np, bitangent), dot(np, eyeVec));
}
float2 GetDirection(uint i, float2 noise)
{
    float angle = (i + noise.x) * (2.0 * PI) / Nd;
    return float2(cos(angle), sin(angle));
}
float ARand21(float2 p)
{
    return frac(sin(dot(p, float2(12.9898, 78.233))) * 43758.5453);
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
    //return float4(factor, factor, factor, factor);
    
    
    
    
    //float2 uv = IN.texCoord;
    //float depth = gDepthTex.Sample(PointSampler, IN.texCoord).r;
    //if (depth >= 1)
    //    return float4(1.0, 0, 0, 1.0);
    
    //float3 p = CalculateWorldFromDepth(depth, IN.texCoord);
    //float3 np = gNormalTex.Sample(PointSampler, IN.texCoord).rgb;
    //np = normalize(np * 2.0 - 1.0);
    ////p = TangentSpacePos(p, np);    
    
    
    ////float2 noiseScale = windowSize / 4.0;
    ////float3 randDir = gNoise.Sample(PointSampler, IN.texCoord * noiseScale).rgb;
    ////float3 tangent = normalize(randDir - np * dot(randDir, np));
    ////float3 bitangent = cross(np, tangent);
    ////float3x3 tbn = transpose(float3x3(tangent, bitangent, np));
    ////
    ////
    ////
    ////
    ////
    //float3 eyeVec = normalize(p);
    //float3 upVec = abs(np.z) < 0.999 ? float3(0, 0, 1) : float3(0, 1, 0);
    //float3 tangent = normalize(cross(upVec, np));
    //float3 bitangent = cross(np, tangent);
    ////np = float3(dot(np, tangent), dot(np, bitangent), dot(np, eyeVec));
    ////p = float3(dot(p, tangent), dot(p, bitangent), dot(p, eyeVec));
    
    //float3 VNormal = np;//float3(dot(np, tangent), dot(np, bitangent), dot(np, eyeVec));
    //float3 VPPos = float3(dot(p, tangent), dot(p, bitangent), dot(p, eyeVec));
    
    //float RandPhiOffset = frac(sin(dot(float2(1.234, frameIndex), float2(12.9898, 78.233))) * 43758.5453);
    //float RandPhi = frac(dot((uv * windowSize) % 4, float2(1, 4)) + frameIndex * 5.0 + RandPhiOffset) * 2.0 * 3.141592;
    
    
    //float3 GIAccum = 0.0;
    //float AOAccum = 0.0;
    
    //// Main directions loop
    //for (int i = 0; i < 4; i++)
    //{
    //    RandPhi += 3.141592 * 0.5;
    //    float2 ssDir = float2(cos(RandPhi), sin(RandPhi));
    
    //    float stepDist = 1.0;
    //    float stepCoeff = thickness + thickness * frac(sin(dot(uv * windowSize * (1.4 + frameIndex), float2(12.9898, 78.233))) * 43758.5453);
    
    //    uint BitMask = 0;
    
    //    // Steps loop
    //    for (int s = 1; s <= 32; s++)
    //    {
    //        float2 sampleUV = uv * windowSize + ssDir * stepDist;
    //        float currentStep = max(1.0, stepDist * stepCoeff);
    //        stepDist += currentStep;
    
    //        float2 sampleUV_Norm = sampleUV / windowSize;
    
    //        // Sample neighbor pixel
    //        float4 SAttr0 = gColor.SampleLevel(PointSampler, sampleUV_Norm, 0);
    //        float4 SAttr1 = gNormalTex.SampleLevel(PointSampler, sampleUV_Norm, 0);
    //        float SDepth = gDepthTex.SampleLevel(PointSampler, sampleUV_Norm, 0);
    
    //        if (SDepth < 0.001)
    //            continue; // skip background
    
    //        // Reconstruct sample view-space position
    //        float4 SClipPos = float4(sampleUV_Norm * 2.0 - 1.0, SDepth, 1.0);
    //        float4 SViewPosH = mul(inverseProjection, SClipPos);
    //        float3 SViewPos = SViewPosH.xyz / SViewPosH.w;
    
    //        // Horizon angle calculation
    //        float NorDot = dot(VNormal, SViewPos - VPPos) - 0.001;
    //        float TanDist = length(SViewPos - VPPos - NorDot * VNormal);
    
    //        float Angle1f = atan2(NorDot, TanDist);
    //        float Angle2f = atan2(NorDot - 0.03 * max(1.0, stepDist * 0.07), TanDist);
    
    //        float Angle1 = max(0.0, ceil(Angle1f / (3.141592 * 0.5) * 32.0));
    //        float Angle2 = max(0.0, floor(Angle2f / (3.141592 * 0.5) * 32.0));
    
    //        // Bitmask calculation
    //        uint SBitMask = ((1u << uint(Angle1)) - 1u) ^ ((1u << uint(Angle2)) - 1u);
    
    //        // Sample LightingBuffer
    //        //float3 DirectLighting = LightingBuffer.SampleLevel(LinearSampler, sampleUV_Norm, 0);
    
    //        // Sample normal
    //        float3 SNormal = SAttr1.rgb * 2.0 - 1.0;
    //        //SNormal = float3(dot(SNormal, tangent), dot(SNormal, bitangent), dot(SNormal, eyeVec));
    
    //        // GI contribution
    //        uint NewBits = SBitMask & (~BitMask);
    //        float BitCount = countbits(NewBits);
    
    //        if (BitCount > 0)
    //        {
    //            float CosWeight = pow(cos(Angle2 * (1.0 / 32.0) * 3.141592), 2.0) - pow(cos(Angle1 * (1.0 / 32.0) * 3.141592), 2.0);
    //            float Lambert = sqrt(max(0.0, dot(SNormal, normalize(VPPos - SViewPos))));
                
    //            GIAccum += (BitCount / max(1.0, Angle1 - Angle2)) * radius * CosWeight * Lambert * SAttr0; // radius = lightCoeff
    //        }
    
    //        // Update Bitmask
    //        BitMask |= SBitMask;
    //    }
    
    //    // AO contribution
    //    AOAccum += 1.0 - float(countbits(BitMask)) / 32.0;
    //}
    
    //// Final normalization
    //float3 GIResult = GIAccum / 4.0;
    //float AOResult = AOAccum / 4.0;
    
    //// Output result
    //return float4(GIResult, AOResult);
    
    
    
    //float2 uv = IN.texCoord;
    //float4 Output = float4(0, 0, 0, 0);

    //float2 fragCoord = uv * windowSize;
    //float4 Attr = gColor.SampleLevel(PointSampler, uv, 0);
    //float depth = gDepthTex.SampleLevel(PointSampler, uv, 0);
    
    //float2 clips = uv * 2 - 1;
    //clips.y = -clips.y;
    
    //// View position reconstruction
    //float4 clipPos = float4(clips, depth, 1.0);
    //float4 viewPosH = mul(inverseProjection, clipPos);
    //float3 Pos = viewPosH.xyz / viewPosH.w;

    //    // Eye vector (forward in view space)
    //float3 Eye = float3(0, 0, 1);

    //    // Tangent space basis
    //float3 up = abs(Eye.z) < 0.999 ? float3(0, 0, 1) : float3(0, 1, 0);
    //float3 Tan = normalize(cross(up, Eye));
    //float3 Bit = cross(Eye, Tan);

    //    // Ray direction (same as original)
    //float3 Dir = normalize(float3((clips) * (windowSize.x / windowSize.y) * tan(30.0), 1.0));

    //    // Geometry
    //float3 PPos = Pos + Dir * depth;
    //float3 Normal = gNormalTex.SampleLevel(PointSampler, uv, 0).rgb * 2.0 - 1.0;

    //    // Screen space horizons
    //Output.rgb = float3(0, 0, 0);
    //float3 VNormal = float3(dot(Normal, Tan), dot(Normal, Bit), dot(Normal, Eye));
    //float3 VPPos = float3(dot(PPos - Pos, Tan), dot(PPos - Pos, Bit), dot(PPos - Pos, Eye));

    //float2 modFC = fmod(fragCoord, 4.0);

    //float RandPhiOffset = ARand21(float2(1.234, fmod(frameIndex * 3.26346, 7.2634)));
    //float RandPhi = (fmod(floor(modFC.x) + floor(modFC.y) * 4.0 + frameIndex * 5.0, 16.0) + RandPhiOffset) * 2.0 * 3.141592 * (1.0 / 64.0);

    //for (int i = 0; i < 4; i++)
    //{
    //    RandPhi += 3.141592 * 0.5;
    //    float2 SSDir = float2(cos(RandPhi), sin(RandPhi));

    //    float stepDist = 1.0;
    //    float stepCoeff = 0.15 + 0.15 * ARand21(uv * (1.4 + fmod(frameIndex * 3.26346, 6.2634)));

    //    uint BitMask = 0;

    //    for (int s = 1; s <= 32; s++)
    //    {
    //        float2 sampleUV_screen = fragCoord + SSDir * stepDist;
    //        float currentStep = max(1.0, stepDist * stepCoeff);
    //        stepDist += currentStep;

    //        if (sampleUV_screen.x < 1.0 || sampleUV_screen.y < 1.0 || sampleUV_screen.x >= windowSize.x - 1.0 || sampleUV_screen.y >= windowSize.y - 1.0)
    //            break;

    //        float2 sampleUV = sampleUV_screen / windowSize;

    //        float4 SAttr = gColor.SampleLevel(PointSampler, sampleUV, 0);
    //        float SDepth = gDepthTex.SampleLevel(PointSampler, sampleUV, 0);

    //        if (SAttr.y < -1.5)
    //            continue;

    //        float2 clipd = sampleUV * 2 - 1;
    //        clipd.y = -clipd.y;
            
    //        float4 SClipPos = float4(clipd, SDepth, 1.0);
    //        float4 SViewPosH = mul(inverseProjection, SClipPos);
    //        float3 SViewPos = SViewPosH.xyz / SViewPosH.w;

    //        float3 SVPPos = normalize(float3(clipd, 1.0) * (windowSize.x / windowSize.y) * tan(30.0)) * SDepth;

    //        float NorDot = dot(VNormal, SVPPos - VPPos) - 0.001;
    //        float TanDist = length(SVPPos - VPPos - NorDot * VNormal);

    //        float Angle1f = atan2(NorDot, TanDist);
    //        float Angle2f = atan2(NorDot - 0.03 * max(1.0, stepDist * 0.07), TanDist);

    //        float Angle1 = max(0.0, ceil(Angle1f / (3.141592 * 0.5) * 32.0));
    //        float Angle2 = max(0.0, floor(Angle2f / (3.141592 * 0.5) * 32.0));

    //        uint SBitMask = ((1u << uint(Angle1)) - 1u) ^ ((1u << uint(Angle2)) - 1u);

    //        float3 SNormal = gNormalTex.SampleLevel(PointSampler, uv, 0).rgb * 2.0 - 1.0;
    //        SNormal = float3(dot(SNormal, Tan), dot(SNormal, Bit), dot(SNormal, Eye));

    //        //float3 DirectLighting = LightingBuffer.SampleLevel(LinearSampler, sampleUV, 0);

    //        uint NewBits = SBitMask & (~BitMask);
    //        float BitCount = countbits(NewBits);

    //        if (BitCount > 0)
    //        {
    //            float CosWeight = pow(cos(Angle2 * (1.0 / 32.0) * 3.141592), 2.0) - pow(cos(Angle1 * (1.0 / 32.0) * 3.141592), 2.0);
    //            float Lambert = sqrt(max(0.0, dot(SNormal, normalize(SVPPos - VPPos))));

    //            Output.rgb += (BitCount / max(1.0, Angle1 - Angle2)) * 10.0 * CosWeight * Lambert * SAttr.rgb;
    //        }

    //        BitMask |= SBitMask;
    //    }
        
    //    Output.a += 1.0 - (countbits(BitMask) / (float) Nb);
    //}
    //Output.a /= 4.0;
    ////Output.rgb = Pos; //Dir;
    //return Output;
    
    
    
    float2 uv = IN.texCoord;
    float depth = gDepthTex.Sample(PointSampler, IN.texCoord).r;
    if (depth >= 1)
        return float4(1.0, 0, 0, 1.0);
    
    float3 p = CalculateViewSpaceFromDepth(depth, IN.texCoord);
    float3 np = gNormalTex.Sample(PointSampler, IN.texCoord).rgb;
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

            float3 sf = CalculateViewSpaceFromDepth(gDepthTex.Sample(PointSampler, offsetUV).r, offsetUV); //SamplePosition(offsetUV);
            float3 nj = normalize(gNormalTex.Sample(PointSampler, offsetUV).rgb * 2.0 - 1.0);
            sf = TangentSpacePos(sf, nj);
            
            
            float3 sb = sf - (thickness * p / length(p));
            float3 delta = sf - p;
            
            float theta_f = atan2(sf.y, sf.x);
            float theta_b = atan2(sb.y, sb.x);
            float thetaMin = min(theta_f, theta_b);
            float thetaMax = max(theta_f, theta_b);
            uint a = uint(floor((thetaMin + PI / 2) / PI * Nb));
            uint b = uint(ceil((thetaMax - thetaMin + PI / 2) / PI * Nb));
            uint bj = ((1u << b) - 1u) ^ ((1u << a) - 1u);
            
            //float3 cj = GBuffer2.Load(int3(sampleCoord, 0)).xyz;
            float3 lj = normalize(sf - p);
            uint newBits = bj & (~bitmask);
            uint bitCount = countbits(newBits);
            // Thickness check
            //if (abs(dot(delta, np)) > thickness)
            //    continue;

            //float3 lj = normalize(delta);
            //float3 nj = normalize(gNormalTex.Sample(PointSampler, offsetUV).rgb * 2.0 - 1.0);
            float3 col = gColor.Sample(PointSampler, offsetUV).rgb;

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
            GI += ( /*cj * */bitCount / float(Nb)) * saturate(dot(np, lj)) * saturate(dot(nj, -lj)) * col * 10.0;
            bitmask |= bj; // 가렸다면 업데이트
        }

        AO += 1.0 - (countbits(bitmask) / (float) Nb);
    }

    AO /= Nd;
    GI /= Nd;
    return float4(GI, AO);
    //float2 noise = frac(sin(dot(uv, uint2(12.9898, 78.233))) * float2(43758.5453, 12345.6789));
    //return float4(GetDirection(0, noise), 0, 1);

}
