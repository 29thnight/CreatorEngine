#include "VolumetricFog.hlsli"
#define PI 3.14159265358
#define EPSILON 0.000001

SamplerState SamplerLinearClamp : register(s0);
SamplerState SamplerLinearWrap : register(s1);
SamplerComparisonState CascadedPcfShadowMapSampler : register(s2);

RWTexture3D<float4> VoxelWriteTexture : register(u0);

Texture2DArray<float> ShadowTexture : register(t0);
Texture2D<float4> BlueNoiseTexture : register(t1);
Texture3D<float4> VoxelReadTexture : register(t2);
Texture2D<float4> CloudShadowMap : register(t3);

cbuffer VolumetricFogCBuffer : register(b0)
{
    float4x4 InvViewProj;
    float4x4 PrevViewProj;
    
    float4x4 ShadowMatrix;
    float4 SunDirection;
    float4 SunColor;
    float4 CameraPosition;
    float4 CameraNearFar_FrameIndex_PreviousFrameBlend;
    float4 VolumeSize;
    float Anisotropy;
    float Density;
    float Strength;
    float ThicknessFactor;
}
cbuffer CloudShadowMapConstants : register(b1)
{
    float4x4 viewProjection;
    float2 cloudMapSize;
    float2 direction;
    uint frameIndex;
    float moveSpeed;
}

float HenyeyGreensteinPhaseFunction(float3 viewDir, float3 lightDir, float g)
{
    float cos_theta = dot(viewDir, lightDir);
    float denom = 1.0f + g * g + 2.0f * g * cos_theta;
    return (1.0f / (4.0f * PI)) * (1.0f - g * g) / max(pow(denom, 1.5f), EPSILON);
}

float GetBlueNoiseSample(uint3 texCoord)
{
    uint width, height;
    BlueNoiseTexture.GetDimensions(width, height);
    uint2 noiseCoord = (texCoord.xy + uint2(0, 1) * texCoord.z * width) % width;
    return BlueNoiseTexture.Load(uint3(noiseCoord, 0)).r;
}
float GetVisibility(float3 voxelWorldPoint, float4x4 svp)
{
    float4 lightSpacePos = mul(svp, float4(voxelWorldPoint, 1.0f));
    float4 ShadowCoord = lightSpacePos / lightSpacePos.w;
    ShadowCoord.rg = ShadowCoord.rg * float2(0.5f, -0.5f) + float2(0.5f, 0.5f);
    
    return ShadowTexture.SampleCmpLevelZero(CascadedPcfShadowMapSampler, float3(ShadowCoord.xy, 2), ShadowCoord.z).r;
}

float GetCloudVisibility(float4 worldPosition)
{
    float2 texelSize = float2(1, 1) / cloudMapSize;
    
    float4 lightSpacePosition = mul(viewProjection, worldPosition);
    float3 projCoords = lightSpacePosition.xyz / lightSpacePosition.w;
    float currentDepth = projCoords.z;
    projCoords.y = -projCoords.y;
    projCoords.xy = (projCoords.xy * 0.5) + 0.5f;
    
    // (uv * size) + (time * moveSpeed * direction) = cloudMove
    float2 uv = float2(projCoords.xy * 4 + frameIndex * 0.00003 * float2(1, 1) /** 0.003 + float2(1, 1) * 0.1f * frameIndex * texelSize*/);
    float closestDepth = CloudShadowMap.SampleLevel(SamplerLinearWrap, uv, 0).r;
    float shadow = 0;
    shadow = closestDepth;
    
    return shadow;
}

[numthreads(8, 8, 1)]
void main(uint3 Gid : SV_GroupID, uint3 GTid : SV_GroupThreadID, uint3 DTid : SV_DispatchThreadID)
{
    int3 texCoord = DTid.xyz;
    
    if (texCoord.x < VolumeSize.x && texCoord.y < VolumeSize.y && texCoord.z < VolumeSize.z)
    {
        float jitter = frac((GetBlueNoiseSample(texCoord) - 0.5f) * (1.0f - EPSILON) * CameraNearFar_FrameIndex_PreviousFrameBlend.z);
        float3 voxelWorldPos = GetWorldPosFromVoxelID(texCoord, jitter, CameraNearFar_FrameIndex_PreviousFrameBlend.x, CameraNearFar_FrameIndex_PreviousFrameBlend.y, InvViewProj, VolumeSize.xyz);
        float3 voxelWorldPosNoJitter = GetWorldPosFromVoxelID(texCoord, 0.0f, CameraNearFar_FrameIndex_PreviousFrameBlend.x, CameraNearFar_FrameIndex_PreviousFrameBlend.y, InvViewProj, VolumeSize.xyz);
        float3 viewDir = normalize(CameraPosition.xyz - voxelWorldPos);
    
        float3 lighting = float3(0.0, 0.0, 0.0);
        float visibility = GetVisibility(voxelWorldPos, ShadowMatrix) * GetCloudVisibility(float4(voxelWorldPos, 1.0));
        //float visibility2 = GetVisibility(voxelWorldPosNoJitter, ShadowMatrix);

        if (visibility > EPSILON)
            lighting += visibility * SunColor.xyz * HenyeyGreensteinPhaseFunction(viewDir, -SunDirection.xyz, Anisotropy);
        
        float4 result = float4(lighting * Strength * Density, visibility * Density);
        
        //previous frame interpolation
        {
            float3 prevUV = GetUVFromVolumetricFogVoxelWorldPos(voxelWorldPosNoJitter,
            CameraNearFar_FrameIndex_PreviousFrameBlend.x, CameraNearFar_FrameIndex_PreviousFrameBlend.y, PrevViewProj, VolumeSize.xyz);
            
            if (prevUV.x >= 0.0f && prevUV.y >= 0.0f && prevUV.z >= 0.0f &&
                prevUV.x <= 1.0f && prevUV.y <= 1.0f && prevUV.z <= 1.0f)
            {
                float4 prevResult = VoxelReadTexture.SampleLevel(SamplerLinearClamp, prevUV, 0.0f);
                result = lerp(result, prevResult, CameraNearFar_FrameIndex_PreviousFrameBlend.w);
            }
        }

        VoxelWriteTexture[texCoord] = result;
    }
}
