#include "Sampler.hlsli"
#include "Lighting.hlsli"
#include "Shading.hlsli"
#include "BRDF.hlsli"

struct PixelShaderInput // see Fullscreen.vs.hlsl
{
    float4 position : SV_POSITION;
    float2 texCoord : TEXCOORD0;
};

cbuffer DeferredCBuffer : register(b3)
{
    float4x4 inverseProjection;
    float4x4 inverseView;
    int useAO;
    int useEnvMap;
    float envMapIntensity;
}

Texture2D DepthTexture : register(t0);
Texture2D Diffuse : register(t1);
Texture2D MetalRough : register(t2);
Texture2D Normals : register(t3);
Texture2D AO : register(t5);

TextureCube EnvMap : register(t6);
TextureCube PrefilteredSpecMap : register(t7);
Texture2D BrdfLUT : register(t8);
Texture2D Emissive : register(t9);
Texture2D<uint> Bitflag : register(t11);

struct gOutput
{
    float4 Default : SV_TARGET0;
    float4 LightEmissive : SV_TARGET1;
};

float3 CalculateWorldFromDepth(float depth, float2 texCoord)
{
    // clip space between [-1, 1]
    // flip y so that +ve y is upwards
    float2 clipXY = texCoord * 2.0 - 1.0;
    clipXY.y = -clipXY.y;

    // NOTE: depth is not linearized
    // Also in range [0, 1] due to DirectX Convention
    float4 clipSpace = float4(clipXY, depth, 1);
    float4 viewSpace = mul(inverseProjection, clipSpace);

    // perspective divide
    viewSpace /= viewSpace.w;

    float4 worldSpace = mul(inverseView, viewSpace);
    return worldSpace.xyz;
}

gOutput main(PixelShaderInput IN) : SV_TARGET
{
    float depth = DepthTexture.Sample(PointSampler, IN.texCoord).r;
    float3 posW = CalculateWorldFromDepth(depth, IN.texCoord);
    float4 matMetalRough = MetalRough.Sample(PointSampler, IN.texCoord);
    float3 albedo = Diffuse.Sample(PointSampler, IN.texCoord).rgb;
    float metallic = matMetalRough.r;
    float roughness = matMetalRough.g;
    float occlusion = matMetalRough.b;
    float3 normal = Normals.Sample(PointSampler, IN.texCoord).rgb;
    normal = normalize(normal * 2.0 - 1.0);
    float3 emissive = Emissive.Sample(PointSampler, IN.texCoord).rgb;
	uint bitflag = Bitflag.Load(int3(IN.texCoord * int2(1920, 1080), 0));

    SurfaceInfo surf;
    surf.posW = float4(posW, 1);
    surf.N = normal;
    surf.V = normalize(eyePosition.xyz - posW);
    surf.NdotV = dot(surf.N, surf.V);

    float3 Lo = float3(0, 0, 0);
    float3 F0 = float3(0.04, 0.04, 0.04);
    F0 = lerp(F0, albedo, metallic);

	uint light_count = lightCount;

    bool useShadowRevice = (bitflag & USE_SHADOW_RECIVE) != 0;

    for (int i = 0; i < light_count; ++i)
    {
        Light light = Lights[i];
        if (light.status == LIGHT_DISABLED)
            continue;
        LightingInfo li = EvalLightingInfo(surf, light);
        float NdotL = saturate(li.NdotL); // clamped n dot l
        float NdotV = saturate(surf.NdotV);
        
        // cook-torrance brdf
        float NDF = DistributionGGX(saturate(li.NdotH), roughness);
        float G = GeometrySmith(NdotV, NdotL, roughness);
        float3 F = fresnelSchlick(saturate(dot(li.H, surf.V)), F0);
        float3 kS = F;
        float3 kD = float3(1.0, 1.0, 1.0) - kS;
        kD *= 1.0 - metallic;
        
        float3 numerator = NDF * G * F;
        float denominator = 4.0 * NdotV * NdotL;
        float3 specular = numerator / max(denominator, 0.001);
        
        //light.color.rgb *= light.intencity;

        Lo += (kD * albedo / PI + specular) * light.color.rgb * li.attenuation * NdotL * (useShadowRevice ? (li.shadowFactor) : 1);

    }
    
    float3 ambient = globalAmbient.rgb * albedo;
    
    [branch]
    if (useEnvMap)
    {
        float3 kS = fresnelSchlickRoughness(max(surf.NdotV, 0.8f), F0, roughness);
        float3 kD = 1.0 - kS;
        kD *= 1.0 - metallic;
        float3 irradiance = EnvMap.Sample(LinearSampler, surf.N).rgb;
        float3 diffuse = irradiance * albedo;
    
        float3 R = normalize(reflect(-surf.V, surf.N));
        float3 prefilterdColour = PrefilteredSpecMap.SampleLevel(LinearSampler, R, roughness * 5.0).rgb;
        float2 envBrdf = BrdfLUT.Sample(PointSampler, float2(max(surf.NdotV, 0.f), roughness)).rg;
        float3 specular = prefilterdColour * (kS * envBrdf.x + envBrdf.y);
        ambient = (kD * diffuse + specular) * envMapIntensity;
    }

    //float ao = useAO ? AO.Sample(PointSampler, IN.texCoord).a : 1.0;
    //float3 GI = useAO ? AO.Sample(PointSampler, IN.texCoord).rgb : float3(0, 0, 0);
    ambient *= occlusion;
    float3 colour = ambient + Lo + emissive; //(ambient + Lo) * ao + emissive;// + GI * ao; //(albedo * GI * ao);
    
    //float3 debugCascade = overlayCascadeDebug(surf.posW);
    
    gOutput output;
    output.Default = float4(colour.rgb, 1.0);
    output.LightEmissive = float4(Lo + emissive, 1.0);
    return output;
    //return float4(colour, 1.0);
}
