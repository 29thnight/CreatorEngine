#ifndef LIGHTING_COMMON
#define LIGHTING_COMMON

#include "Sampler.hlsli"
#include "Flags.hlsli"

#define MAX_CASCADE 4
#define MAX_LIGHTS 255
#define DIRECTIONAL_LIGHT 0
#define POINT_LIGHT 1
#define SPOT_LIGHT 2

#define LIGHT_DISABLED 0
#define LIGHT_ENABLED 1
#define LIGHT_ENABLED_W_SHADOWMAP 2

//Texture2D ShadowMap : register(t4); // support 1 for now, future use array
Texture2DArray ShadowMapArr : register(t4); //< -- t4 
Texture2D CloudShadowMap : register(t10);

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

cbuffer LightProperties : register(b1)
{
    float4 eyePosition;
    float4 globalAmbient;
    Light Lights[MAX_LIGHTS];
}

cbuffer LightCount : register(b11)
{
    uint lightCount;
}

cbuffer ShadowMapConstants : register(b2) // supports one
{
    float mapWidth;
    float mapHeight;
   
    float4x4 lightViewProjection[3];

    float m_casCadeEnd1;
    float m_casCadeEnd2;
    float m_casCadeEnd3;
    float _epsilon;
    int devideShadow; //max = 9
    uint useCasCade;
    
}
cbuffer CloudShadowMapConstants : register(b4)
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

cbuffer CameraView : register(b10)
{
    matrix cameraview;
}

float3 overlayCascadeDebug(float4 worldPosition)
{
    float4 viewPos = mul(cameraview, float4(worldPosition.xyz, 1.0f));
    float cascadeIndex = (viewPos.z <= m_casCadeEnd1) ? 0 :
              (viewPos.z <= m_casCadeEnd2) ? 1 :
              (viewPos.z <= m_casCadeEnd3) ? 2 : 2;
    float3 color = (cascadeIndex == 0) ? float3(1, 0, 0)
              : (cascadeIndex == 1) ? float3(0, 1, 0)
              : (cascadeIndex == 2) ? float3(0, 0, 1)
              : float3(1, 1, 0);
    return color;
}

float CloudShadowFactor(float4 worldPosition)
{
    //float2 cloudUV = worldPosition.xz * 0.003 + frameIndex * 0.00005 * float2(0.9, 0.2); // * cloudScale + cloudOffset;)
    //float shadow = 0;
    //for (int x = -1; x < 2; ++x)
    //{
    //    //[unroll]
    //    for (int y = -1; y < 2; ++y)
    //    {
    //        float2 uv = float2(cloudUV + float2(x, y) / cloudMapSize * 0.3);
    //        float depth = CloudShadowMap.Sample(LinearSampler, uv).r;
    //        shadow += depth;
    //    }
    //}
    //shadow /= 9.0;
    //return shadow;
    if (isOn)
    {
        float2 texelSize = float2(1, 1) / cloudMapSize;
    
        float4 lightSpacePosition = mul(viewProjection, worldPosition);
        float3 projCoords = lightSpacePosition.xyz / lightSpacePosition.w;
        float currentDepth = projCoords.z;
        projCoords.y = -projCoords.y;
        projCoords.xy = (projCoords.xy * 0.5) + 0.5f;
    
        // (uv * size) + (time * moveSpeed * direction) = cloudMove
        float2 uv = float2(projCoords.xy * size + frameIndex * moveSpeed * direction /** 0.003 + float2(1, 1) * 0.1f * frameIndex * texelSize*/);
        float closestDepth = CloudShadowMap.Sample(LinearSampler, uv).r;
        float shadow = 0;
        shadow = closestDepth;
    
        return saturate(shadow * alpha);
    }
    else 
        return 1;
}

float ShadowFactor(float4 worldPosition) // assumes only one shadow map cbuffer
{
    
    
    
    float4 viewPos = mul(cameraview, float4(worldPosition.xyz, 1.0f));
    float m_casCadeEnd[3];
    int nextShadowIndex;
    int shadowIndex = 0;
    if (useCasCade)
    {
        m_casCadeEnd[0] = m_casCadeEnd1;
        m_casCadeEnd[1] = m_casCadeEnd2;
        m_casCadeEnd[2] = m_casCadeEnd3;
        shadowIndex = (viewPos.z <= m_casCadeEnd1) ? 0 :
              (viewPos.z <= m_casCadeEnd2) ? 1 :
              (viewPos.z <= m_casCadeEnd3) ? 2 : 2;
  
        nextShadowIndex = min(shadowIndex + 1, 2);
        //if ((viewPos.x) <= m_casCadeEnd1 &&
        //     (viewPos.y) <= m_casCadeEnd1 &&
        //     (viewPos.z) <= m_casCadeEnd1)
        //{
        //    shadowIndex = 0;
        //}
        //else if ((viewPos.x) <= m_casCadeEnd2 &&
        // (viewPos.y) <= m_casCadeEnd2 &&
        // (viewPos.z) <= m_casCadeEnd2)
        //{
        //    shadowIndex = 1;
        //}
        //else if ((viewPos.x) <= m_casCadeEnd3 &&
        // (viewPos.y) <= m_casCadeEnd3 &&
        // (viewPos.z) <= m_casCadeEnd3)
        //{
        //    shadowIndex = 2;
        //}

        //nextShadowIndex = min(shadowIndex + 1, 2);
    }
    else
    {
        shadowIndex = 0;
        nextShadowIndex = 0;

    }
    //shadowIndex = 2;
    float2 texelSize = float2(1, 1) / float2(mapWidth, mapHeight);
    float epsilon = _epsilon;
    //그림자 경게선 퍼센트 
    float cascadeEdge = 0.1f;
    float startZ = m_casCadeEnd[shadowIndex];
    float endZ = m_casCadeEnd[nextShadowIndex];
    
   
    
    float4 lightSpacePosition = mul(lightViewProjection[shadowIndex], worldPosition);
    float3 projCoords = lightSpacePosition.xyz / lightSpacePosition.w;
    float currentDepth = projCoords.z;

    if (currentDepth > 1)
        return 0;
    

    projCoords.y = -projCoords.y;
    projCoords.xy = (projCoords.xy * 0.5) + 0.5f;
    float shadow = 0;
    //[unroll]
    if (projCoords.x >= 0.0 && projCoords.x <= 1.0 && projCoords.y >= 0.0 && projCoords.y <= 1.0)
    {
        for (int x = -1; x < 2; ++x)
        {
        //[unroll]
            for (int y = -1; y < 2; ++y)
            {
                float2 uv = float2(projCoords.xy + float2(x, y) * texelSize);
                float closestDepth = ShadowMapArr.Sample(PointSampler, float3(uv, shadowIndex)).r;
                shadow += (closestDepth < currentDepth - epsilon);
            }
        }
    }
    shadow /= devideShadow;
    float finalShadow = shadow;
    
    if(useCasCade)
    {
    
        if (viewPos.z > endZ - cascadeEdge && shadowIndex < 2)
        {
        
            float4 nextlightSpacePosition = mul(lightViewProjection[nextShadowIndex], worldPosition);
            float3 nextprojCoords = nextlightSpacePosition.xyz / nextlightSpacePosition.w;
            float nextcurrentDepth = nextprojCoords.z;

            if (nextcurrentDepth > 1)
                return 0;
    

            nextprojCoords.y = -nextprojCoords.y;
            nextprojCoords.xy = (nextprojCoords.xy * 0.5) + 0.5f;
            float nextshadow = 0;
            float blendFactor = saturate((viewPos.z - (endZ - cascadeEdge)) / cascadeEdge);
            if (nextprojCoords.x >= 0.0 && nextprojCoords.x <= 1.0 && nextprojCoords.y >= 0.0 && nextprojCoords.y <= 1.0)
            {
                for (int x = -1; x < 2; ++x)
                {
        //[unroll]
                    for (int y = -1; y < 2; ++y)
                    {
                        float2 uv = float2(nextprojCoords.xy + float2(x, y) * texelSize);
                        float closestDepth = ShadowMapArr.Sample(PointSampler, float3(uv, nextShadowIndex)).r;
                        nextshadow += (closestDepth < nextcurrentDepth - epsilon);
                    }
                }
            }
            nextshadow /= devideShadow;
            finalShadow = lerp(shadow, nextshadow, blendFactor);
        
        }
    }
    
 
    return finalShadow;
}
#endif
