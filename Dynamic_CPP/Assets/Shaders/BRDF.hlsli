#ifndef BRDF_COMMON
#define BRDF_COMMON

//#include "Shading.hlsli"

#ifndef PI
#define PI 3.14159265359
#endif

float3 fresnelSchlick(float cosTheta, float3 F0)
{
    return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}

float3 fresnelSchlickRoughness(float cosTheta, float3 F0, float roughness)
{
    float a = 1.0 - roughness;
    return F0 + (max(float3(a, a, a), F0) - F0) * pow(1.0 - cosTheta, 5.0);
}

float DistributionGGX(float NdotH, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH2 = NdotH * NdotH;

    float num = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;
    
    denom = max(denom, 0.001);

    return num / denom;
}

float GeometrySchlickGGX(float NdotX, float roughness)
{
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;

    float num = NdotX;
    float denom = NdotX * (1.0 - k) + k;

    return num / denom;
}

float GeometrySchlickGGXIBL(float NdotX, float roughness)
{
    float r = roughness;
    float k = (r * r) / 2.0;

    float num = NdotX;
    float denom = NdotX * (1.0 - k) + k;

    return num / denom;
}

float GeometrySmith(float NdotV, float NdotL, float roughness)
{
    float ggx2 = GeometrySchlickGGX(NdotV, roughness);
    float ggx1 = GeometrySchlickGGX(NdotL, roughness);

    return ggx1 * ggx2;
}

float GeometrySmithIBL(float NdotV, float NdotL, float roughness)
{
    float ggx2 = GeometrySchlickGGXIBL(NdotV, roughness);
    float ggx1 = GeometrySchlickGGXIBL(NdotL, roughness);

    return ggx1 * ggx2;
}

float RadicalInverse_VdC(uint bits)
{
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10; // / 0x100000000
}

float2 Hammersley(uint i, uint N)
{
    return float2(float(i) / float(N), RadicalInverse_VdC(i));
}


float3 ImportanceSampleGGX(float2 st, float3 N, float roughness)
{
    float a = roughness * roughness;

    float phi = 2.0 * PI * st.x;
    float cosTheta = sqrt((1.0 - st.y) / (1.0 + (a * a - 1.0) * st.y));
    float sinTheta = sqrt(1.0 - cosTheta * cosTheta);

    // from spherical coordinates to cartesian coordinates
    float3 H;
    H.x = cos(phi) * sinTheta;
    H.y = sin(phi) * sinTheta;
    H.z = cosTheta;

    // from tangent-space vector to world-space sample vector
    float3 up = abs(N.z) < 0.999 ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0);
    float3 tangent = normalize(cross(up, N));
    float3 bitangent = cross(N, tangent);

    float3 sampleVec = tangent * H.x + bitangent * H.y + N * H.z;
    return normalize(sampleVec);
}

// ���� ������.
//float3 BRDF_PBR(float3 albedo, float3 normal, float roughness, float metallic, Light light, SurfaceInfo surf)
//{
//    float3 F0 = float3(0.04, 0.04, 0.04);
//    F0 = lerp(F0, albedo, metallic);
    
//    LightingInfo li = EvalLightingInfo(surf, light);

//        // cook-torrance brdf
//    float NDF = DistributionGGX(saturate(li.NdotH), roughness);
//    float G = GeometrySmith(saturate(surf.NdotV), saturate(li.NdotL), roughness);
//    float3 F = fresnelSchlick(saturate(dot(li.H, surf.V)), F0);
//    float3 kS = F;
//    float3 kD = float3(1.0, 1.0, 1.0) - kS;
//    kD *= 1.0 - metallic;

//    float NdotL = saturate(li.NdotL); // clamped n dot l

//    float3 numerator = NDF * G * F;
//    float denominator = 4.0 * saturate(surf.NdotV) * NdotL;
//    float3 specular = numerator / max(denominator, 0.001);
//    float3 diffuse = kD * albedo / PI;
    
//    return (diffuse + specular) * light.color.rgb * li.attenuation * NdotL * (li.shadowFactor);
//}

#endif
