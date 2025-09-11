#include "VolumetricFog.hlsli"
#define PI 3.14159265358
#define EPSILON 0.000001
#define LIGHT_DISABLED 0
#define DIRECTIONAL_LIGHT 0
#define POINT_LIGHT 1
#define SPOT_LIGHT 2
#define MAX_LIGHTS 20

SamplerState SamplerLinearClamp : register(s0);
SamplerState SamplerLinearWrap : register(s1);
SamplerComparisonState CascadedPcfShadowMapSampler : register(s2);

RWTexture3D<float4> VoxelWriteTexture : register(u0);

Texture2DArray<float> ShadowTexture : register(t0);
Texture2D<float4> BlueNoiseTexture : register(t1);
Texture3D<float4> VoxelReadTexture : register(t2);
Texture2D<float4> CloudShadowMap : register(t3);

struct Light
{
    float4 position;
    float4 direction;
    float4 color;

    float constantAtt;
    float linearAtt;
    float quadAtt;
    float spotAngle;

    int lightType;
    int status;
    float range;
    float intencity;
};

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
    float2 size;
    float2 direction;
    uint frameIndex;
    float moveSpeed;
    float alpha;
    int isOn;
}

cbuffer LightProperties : register(b2)
{
    float4 eyePosition;
    float4 globalAmbient;
    Light Lights[MAX_LIGHTS];
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
    float2 uv = float2(projCoords.xy * size + frameIndex * moveSpeed * direction /** 0.003 + float2(1, 1) * 0.1f * frameIndex * texelSize*/);
    float closestDepth = CloudShadowMap.SampleLevel(SamplerLinearWrap, uv, 0).r;
    float shadow = 0;
    shadow = closestDepth;
    
    return shadow * alpha;
}

// 거리에 따른 빛의 감쇠 계산
float GetDistanceAttenuation(float distance, float lightRange)
{
      // 거리가 라이트의 최대 범위를 넘어서면 빛이 없음
    if (distance > lightRange)
        return 0.0f;

      // 간단한 선형 감쇠 또는 1/d^2 기반의 감쇠 사용 가능
    float attenuation = 1.0f - saturate(distance / lightRange);
    return attenuation * attenuation; // 제곱하여 좀 더 자연스러운 감쇠 곡선 생성
}

  // 스포트라이트 원뿔 각도에 따른 감쇠 계산
float GetSpotAttenuation(float3 lightDir, float3 spotDir, float2 spotAngles)
{
    float cosOuter = spotAngles.x; // cos(outerAngle)
    float cosInner = spotAngles.y; // cos(innerAngle)

    //float cosAngle = dot(spotDir, lightDir);
    float cosAngle = dot(lightDir, spotDir);

      // smoothstep을 사용하여 내부 원뿔과 외부 원뿔 사이를 부드럽게 보간
    return smoothstep(cosOuter, cosInner, cosAngle);
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

        //if (visibility > EPSILON)
        //    lighting += visibility * SunColor.xyz * HenyeyGreensteinPhaseFunction(viewDir, -SunDirection.xyz, Anisotropy);
        
        for (int i = 0; i < MAX_LIGHTS; i++)
        {
            Light light = Lights[i];
            if (light.status == LIGHT_DISABLED)
                continue;
            
            float3 lightDir;
            float attenuation = 1.0f;
            switch (abs(light.lightType))
            {
                case DIRECTIONAL_LIGHT:
                    {
                    lightDir = normalize(light.direction.xyz);
                    attenuation = 1.0f; // 방향 광원은 감쇠 없음
                    break;
                    }
                case POINT_LIGHT:
                    {
                    float3 lightVec = light.position.xyz - voxelWorldPos;
                    float distanceToLight = length(lightVec);
                    lightDir = normalize(lightVec);
                    attenuation = GetDistanceAttenuation(distanceToLight, light.range);
                    break;
                    }
                case SPOT_LIGHT:
                    {
                    float3 lightVec = light.position.xyz - voxelWorldPos;
                    float distanceToLight = length(lightVec);
                    lightDir = normalize(lightVec);
                    float minCos = cos(light.spotAngle);
                    float maxCos = (minCos + 1.0f) / 2.0f; // squash between [0, 1]
                    float distAtt = GetDistanceAttenuation(distanceToLight, light.range);
                    float spotAtt = GetSpotAttenuation(-lightDir, normalize(light.direction.xyz), float2(minCos, maxCos));
                    attenuation = distAtt * spotAtt;
                    break;
                    }
                default:
                    break;
            }
            if (attenuation > EPSILON)
            {
                float phase = HenyeyGreensteinPhaseFunction(viewDir, -lightDir, Anisotropy);
                lighting += light.color.rgb * phase * attenuation;
            }
        }
        
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
