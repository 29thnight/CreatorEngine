#ifndef LIGHTING_COMMON
#define LIGHTING_COMMON

#include "Sampler.hlsli"

#define MAX_LIGHTS 4
#define DIRECTIONAL_LIGHT 0
#define POINT_LIGHT 1
#define SPOT_LIGHT 2

#define LIGHT_DISABLED 0
#define LIGHT_ENABLED 1
#define LIGHT_ENABLED_W_SHADOWMAP 2

//Texture2D ShadowMap : register(t4); // support 1 for now, future use array
Texture2DArray ShadowMapArr : register(t4); //< -- t4 
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
    
}

cbuffer CameraView : register(b10)
{
    matrix cameraview;
}

float ShadowFactor(float4 worldPosition) // assumes only one shadow map cbuffer
{
    
    int shadowIndex = 0;
    
    float4 viewPos = mul(cameraview, float4(worldPosition.xyz, 1.0f));
    float m_casCadeEnd[3] = { m_casCadeEnd1, m_casCadeEnd2, m_casCadeEnd3 };
    shadowIndex = (viewPos.z <= m_casCadeEnd1) ? 0 :
              (viewPos.z <= m_casCadeEnd2) ? 1 :
              (viewPos.z <= m_casCadeEnd3) ? 2 : 0;
  
    int nextShadowIndex = min(shadowIndex + 1, 2);
    
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
    
 
    return finalShadow;
}
#endif
