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

cbuffer MeshRendererBuffer : register(b7)
{
    uint bitflag;
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
    float ior = 1.5;
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
    //float3 F0 = float3(0.04, 0.04, 0.04);
    //F0 = lerp(F0, albedo.rgb, metallic);
    
    float3 F0_dielectric = pow((1 - ior) / (1 + ior), 2);
    F0_dielectric = lerp(F0_dielectric, albedo.rgb, metallic);
    
    bool useShadowRevice = (bitflag & USE_SHADOW_RECIVE) != 0;
    
    if ((bitflag & USE_SSRefraction) == 0)
    {
        for (int i = 0; i < lightCount; ++i)
        {
            Light light = Lights[i];
            if (light.status == LIGHT_DISABLED)
                continue;
            LightingInfo li = EvalLightingInfo(surf, light);
            float NdotL = max(li.NdotL, 0.0); // clamped n dot l
            float NdotV = max(surf.NdotV, 0.0);
        
        // cook-torrance brdf
            float NDF = DistributionGGX(max(li.NdotH, 0.0), roughness);
            float G = GeometrySmith(NdotV, NdotL, roughness);
            float3 F = fresnelSchlick(max(dot(li.H, surf.V), 0.0), F0_dielectric);
            float3 kS = F;
            float3 kD = float3(1.0, 1.0, 1.0) - kS;
            kD *= 1.0 - metallic;


            float3 numerator = NDF * G * F;
            float denominator = 4.0 * NdotV * NdotL;
            float3 specular = numerator / max(denominator, 0.001);
        
        //light.color.rgb *= light.intencity;

            Lo += (kD * albedo.rgb / PI + specular) * light.color.rgb * li.attenuation * NdotL * (useShadowRevice ? (li.shadowFactor) : 1);
        }

        float3 ambient = globalAmbient.rgb * albedo.rgb;
        if (useEnvMap)
        {
            float3 kS = fresnelSchlickRoughness(saturate(surf.NdotV), F0_dielectric, roughness);
            float3 kD = 1.0 - kS;
            kD *= 1.0 - metallic;
            float3 irradiance = EnvMap.Sample(LinearSampler, surf.N).rgb;
            float3 diffuse = irradiance * albedo.rgb;

            float3 R = normalize(reflect(-surf.V, surf.N));
            uint w, h, mips;
            PrefilteredSpecMap.GetDimensions(0, w, h, mips);
            float3 prefilterdColour = PrefilteredSpecMap.SampleLevel(LinearSampler, R, roughness * (mips - 1)).rgb;
            float2 envBrdf = BrdfLUT.Sample(PointSampler, float2(max(surf.NdotV, 0.f), roughness)).rg;
            float3 specular = prefilterdColour * (kS * envBrdf.x + envBrdf.y);
            ambient = (kD * diffuse + specular);
        }
    
        float3 colour = ambient + Lo + emissive.rgb;

        return float4(colour, albedo.a);
    }
    else
    {
    
    //float2 screenUV = IN.position.xy / IN.position.w;
    //screenUV = screenUV * 0.5 + 0.5;
    //float3 baseNormal = baseNormalTexture.Sample(PointSampler, screenUV).xyz * 2 - 1;
        float3 view = -surf.V;
        float3 refractionColor;
    
        refractionColor.r = Refraction(IN.wPosition.xyz, view, surf.N, 1 / gIOR).r;
        refractionColor.g = Refraction(IN.wPosition.xyz, view, surf.N, 1 / (gIOR + dadd)).g;
        refractionColor.b = Refraction(IN.wPosition.xyz, view, surf.N, 1 / (gIOR + dadd * 2)).b;
        
        float3 transmissionColor = refractionColor * albedo.rgb;
        
        float3 reflectionColor = float3(0, 0, 0);
        
        for (int i = 0; i < lightCount; ++i)
        {
            Light light = Lights[i];
            if (light.status == LIGHT_DISABLED)
                continue;
            LightingInfo li = EvalLightingInfo(surf, light);
            float NdotL = max(li.NdotL, 0.0); // clamped n dot l
            float NdotV = max(surf.NdotV, 0.0);
        
        // cook-torrance brdf
            float NDF = DistributionGGX(max(li.NdotH, 0.0), roughness);
            float G = GeometrySmith(NdotV, NdotL, roughness);
            float3 F = fresnelSchlick(max(dot(li.H, surf.V), 0.0), F0_dielectric);
            float3 kS = F;
            float3 kD = float3(1.0, 1.0, 1.0) - kS;
            kD *= 1.0 - metallic;


            float3 numerator = NDF * G * F;
            float denominator = 4.0 * NdotV * NdotL;
            float3 specular = numerator / max(denominator, 0.001);
        
        //light.color.rgb *= light.intencity;

            reflectionColor += (specular) * light.color.rgb * li.attenuation * NdotL * (useShadowRevice ? (li.shadowFactor) : 1);
        }
        float3 ambient = globalAmbient.rgb * albedo.rgb;
        if (useEnvMap)
        {
            float3 kS = fresnelSchlickRoughness(saturate(surf.NdotV), F0_dielectric, roughness);
            float3 kD = 1.0 - kS;
            kD *= 1.0 - metallic;
            float3 irradiance = EnvMap.Sample(LinearSampler, surf.N).rgb;
            float3 diffuse = irradiance * albedo.rgb;

            float3 R = normalize(reflect(-surf.V, surf.N));
            uint w, h, mips;
            PrefilteredSpecMap.GetDimensions(0, w, h, mips);
            float3 prefilterdColour = PrefilteredSpecMap.SampleLevel(LinearSampler, R, roughness * (mips - 1)).rgb;
            float2 envBrdf = BrdfLUT.Sample(PointSampler, float2(max(surf.NdotV, 0.f), roughness)).rg;
            float3 specular = prefilterdColour * (kS * envBrdf.x + envBrdf.y);
            reflectionColor += (specular);
        }
        
        float3 fresnel = fresnelSchlickRoughness(saturate(surf.NdotV), F0_dielectric, roughness);
        float3 finalColor = transmissionColor * (1.0 - fresnel) + reflectionColor * fresnel;
        float dott = abs(dot(surf.N, surf.V));
        finalColor = finalColor * dott + gAlbedo.rgb * pow(1.0 - dott, 5) * 15;
        float3 colour = finalColor + emissive.rgb;
        return float4(colour, 1);
    }
}
