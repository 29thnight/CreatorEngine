#pragma once

#include "TangentFrame.slang"

float3 PbrUnitNormal(float3 value, float3 fallback)
{
    const float lengthSquared = dot(value, value);
    return all(isfinite(value)) && lengthSquared > 1e-20
        ? value * rsqrt(lengthSquared) : fallback;
}

// Row-vector convention: normals use inverse transpose, T/B use the linear
// transform. Compose skin * world first, retaining the authored bitangent sign.
void TransformPbrFrame(float3 normal, float3 tangent, float3 bitangent,
    float3x3 transform, out float3 worldNormal, out float3 worldTangent,
    out float3 worldBitangent)
{
    const float3 largest = max(abs(transform[0]), max(abs(transform[1]), abs(transform[2])));
    const float scale = max(largest.x, max(largest.y, largest.z));
    const float3 fallback = PbrUnitNormal(normal, float3(0, 0, 1));
    worldNormal = fallback;
    worldTangent = worldBitangent = float3(0, 0, 0);
    if (!isfinite(scale) || scale <= 0.0) return;

    // Removing uniform magnitude avoids underflow for small objects and keeps
    // the tangent-degeneracy threshold independent of their world-space size.
    const float3x3 linear = transform / scale;
    worldTangent = mul(tangent, linear);
    worldBitangent = mul(bitangent, linear);
    const float3 c0 = cross(linear[1], linear[2]);
    const float3 c1 = cross(linear[2], linear[0]);
    const float3 c2 = cross(linear[0], linear[1]);
    const float determinant = dot(linear[0], c0);
    if (abs(determinant) > 1e-8)
    {
        // Only determinant sign is needed after normalization.
        worldNormal = PbrUnitNormal(mul(normal, float3x3(c0, c1, c2))
            * (determinant < 0.0 ? -1.0 : 1.0), fallback);
    }
    else
    {
        // A collapsed/ill-conditioned transform has no stable inverse. Keep a
        // finite geometric fallback and disable tangent-space perturbation.
        worldNormal = PbrUnitNormal(mul(normal, linear), fallback);
        worldTangent = worldBitangent = float3(0, 0, 0);
    }
}

struct PbrMaterial
{
    float4 albedo;
    float roughness;
    float metallic;
    float3 emissive;
};

// Inputs are sampled values or semantic defaults. Keep GBuffer's raw factor
// products; lighting clamps roughness/metallic after the attachment boundary.
PbrMaterial EvaluatePbrMaterial(float4 baseSample, float3 mrSample,
    float3 emissiveSample, float4 baseFactor, float roughnessFactor,
    float metallicFactor, float3 emissiveFactor, float emissiveStrength)
{
    PbrMaterial material;
    material.albedo = baseSample * baseFactor;
    material.roughness = mrSample.g * roughnessFactor;
    material.metallic = mrSample.b * metallicFactor;
    material.emissive = emissiveSample * emissiveFactor * emissiveStrength;
    return material;
}

float3 EvaluatePbrNormal(float3 normal, float3 tangent, float3 bitangent,
    float3 normalSample, float normalScale, bool useNormalMap, bool reduced)
{
    const float3 geometricNormal = normalize(normal);
    float3 result = geometricNormal;
    if (useNormalMap)
    {
        const TangentFrame frame = BuildTangentFrame(normal, tangent, bitangent);
        result = ApplyTangentSpaceNormal(frame, geometricNormal,
            normalSample * 2.0 - 1.0, normalScale);
    }
    return reduced ? normalize(result + geometricNormal) : result;
}

// Independent AO.R, strength 0 is neutral and strength 1 uses the texture.
float EvaluatePbrOcclusion(float sample, float strength)
{
    return lerp(1.0, saturate(sample), saturate(strength));
}
