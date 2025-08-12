#include "Lighting.hlsli"
#include "BRDF.hlsli"
#include "Shading.hlsli"

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

    bool bitflag;
}

cbuffer ForwardCBuffer : register(b3)
{
    int useEnvMap;
    float envMapIntensity;
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

float4 main(PixelShaderInput IN) : SV_TARGET
{
    SurfaceInfo surf;
    surf.posW = IN.wPosition;
    surf.T = normalize(IN.tangent);
    surf.B = normalize(IN.binormal);
    surf.N = normalize(IN.normal);
    
    if (gNormalState == NORMAL_MAP)
    {
        surf.N = CalcNormalFromNormMap(NormalMap, IN.texCoord, surf);
    }
    else if (gNormalState == BUMP_MAP)
    {
        surf.N = CalcNormalFromBumpMap(NormalMap, IN.texCoord, surf);
    }
    surf.V = normalize(eyePosition.xyz - IN.wPosition.xyz);
    surf.NdotV = dot(surf.N, surf.V);

    float4 albedo = gAlbedo;
    [branch]
    if (gUseAlbedoMap)
    {
        albedo = Albedo.Sample(LinearSampler, IN.texCoord);
        if (gConvertToLinear)
            albedo = SRGBtoLINEAR(albedo);
        albedo.a *= gAlbedo.a;
        //albedo.a = lerp(albedo.a, 1, surf.NdotV);
        //albedo.a = pow(albedo.a, 1 / 1.2);  
    }
    
    if (albedo.a == 0.f) {
        discard;
    }
    
    float occlusion = 1;
    
    float metallic = gMetallic;
    float roughness = gRoughness;
    [branch]
    if (gUseOccMetalRough)
    {
        float3 occRoughMetal = OcclusionRoughnessMetal.Sample(LinearSampler, IN.texCoord).rgb;
        occlusion = occRoughMetal.r;
        roughness = occRoughMetal.g;
        metallic = occRoughMetal.b;
    }
    
    float4 emissive = float4(0.0, 0.0, 0.0, 0.0);
    [branch]
    if (gUseEmmisive)
    {
        emissive = Emissive.Sample(LinearSampler, IN.texCoord);
        if (gConvertToLinear)
            emissive = SRGBtoLINEAR(emissive);

    }
    
    roughness = max(roughness, 0.01f);
    
    float3 Lo = float3(0, 0, 0);
    float3 F0 = float3(0.04, 0.04, 0.04);
    F0 = lerp(F0, albedo.rgb, metallic);

    bool useShadowRevice = (bitflag & (1<<8)) != 0;

    for (int i = 0; i < 4; ++i)
    {
        Light light = Lights[i];
        if (light.status == LIGHT_DISABLED)
            continue;
        LightingInfo li = EvalLightingInfo(surf, light);

        // cook-torrance brdf
        float NDF = DistributionGGX(max(0.0, li.NdotH), roughness);
        float G = GeometrySmith(saturate(surf.NdotV), saturate(li.NdotL), roughness);
        float3 F = fresnelSchlick(max(dot(li.H, surf.V), 0.0), F0);
        float3 kS = F;
        float3 kD = float3(1.0, 1.0, 1.0) - kS;
        kD *= 1.0 - metallic;

        float NdotL = saturate(li.NdotL); // clamped n dot l

        float3 numerator = NDF * G * F;
        float denominator = 4.0 * saturate(surf.NdotV) * NdotL;
        float3 specular = numerator / max(denominator, 0.001);

        Lo += (kD * albedo.rgb / PI + specular) * light.color.rgb * li.attenuation * NdotL * (useShadowRevice ? (li.shadowFactor) : 1);

    }

    float3 ambient = globalAmbient.rgb * albedo.rgb;
    if (useEnvMap)
    {
        float3 kS = fresnelSchlickRoughness(saturate(surf.NdotV), F0, roughness);
        float3 kD = 1.0 - kS;
        kD *= 1.0 - metallic;
        float3 irradiance = EnvMap.Sample(LinearSampler, surf.N).rgb;
        float3 diffuse = irradiance * albedo.rgb;

        float3 R = normalize(reflect(-surf.V, surf.N));
        float3 prefilterdColour = PrefilteredSpecMap.SampleLevel(LinearSampler, R, roughness * 5.0).rgb;
        float2 envBrdf = BrdfLUT.Sample(PointSampler, float2(saturate(surf.NdotV), roughness)).rg;
        float3 specular = prefilterdColour * (kS * envBrdf.x + envBrdf.y);
        ambient = (kD * diffuse + specular);
    }
    
    float3 colour = ambient + Lo + emissive.rgb;

    return float4(colour, albedo.a);
}
