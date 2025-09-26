#include "Lighting.hlsli"
#include "BRDF.hlsli"
#include "Shading.hlsli"
#include "Flags.hlsli"

#define VERTEX_NORMALS 0
#define NORMAL_MAP 1
#define BUMP_MAP 2

#ifndef PI
#define PI 3.14159265359
#endif

Texture2D Albedo : register(t0);
Texture2D NormalMap : register(t1);
Texture2D OcclusionRoughnessMetal : register(t2);
Texture2D AoMap : register(t3);
Texture2D Emissive : register(t5);

TextureCube EnvMap : register(t6);
TextureCube PrefilteredSpecMap : register(t7);
Texture2D BrdfLUT : register(t8);

Texture2D baseColorTexture : register(t15);
Texture2D baseNormalTexture : register(t16);
cbuffer MatrixBuffer : register(b12)
{
    matrix viewMat;
    matrix projMat;
    float dadd;
};


cbuffer PBRMaterial : register(b0)
{
    float4 gAlbedo;
    float gMetallic;
    float gRoughness;

    int gUseAlbedoMap;
    int gUseOccMetalRough;
    int gUseAoMap;
    int gUseEmmisive;
    int gNormalState;
    int gConvertToLinear;

    uint bitflag;
    float gIOR;
}

cbuffer ForwardCBuffer : register(b3)
{
    int useEnvMap;
    float envMapIntensity;
}

cbuffer TimeBuffer : register(b5)
{
    float totalTime;
    float deltaTime;
    uint totalFrame;
}

struct PixelShaderInput
{
    float4 position : SV_POSITION;
    float4 pos : POSITION0;
    float4 wPosition : POSITION1;
    float3 normal : NORMAL;
    float3 tangent : TANGENT;
    float3 binormal : BINORMAL;
    float2 texCoord : TEXCOORD0;
    float2 texCoord1 : TEXCOORD1;
};

float3 Refraction(float3 worldPos, float3 view, float3 baseNormal, float ior)
{
    float3 refractPos = refract(normalize(view), normalize(baseNormal), ior);
    refractPos = refractPos + worldPos;
    matrix vp = mul(projMat, viewMat);
    float4 projectedPos = mul(vp, float4(refractPos, 1.0));
    projectedPos.xyz /= projectedPos.w;
    float2 refractUV = projectedPos.xy * 0.5 + 0.5;
    refractUV.y = 1.0 - refractUV.y;
    return baseColorTexture.Sample(PointSampler, refractUV).rgb;
}

float4 main(PixelShaderInput IN) : SV_TARGET
{
    return float4(gAlbedo.rgb * gAlbedo.a * 3.f, gAlbedo.a);
}
