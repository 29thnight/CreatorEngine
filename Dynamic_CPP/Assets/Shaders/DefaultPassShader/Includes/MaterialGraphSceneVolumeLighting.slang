#pragma once
#include "MaterialGraphSceneVolume.slang"

#ifndef LX_VOLUME_CONSTANTS_REGISTER
#define LX_VOLUME_CONSTANTS_REGISTER b1
#define LX_VOLUME_SHADOW_REGISTER t19
#define LX_VOLUME_ENVIRONMENT_REGISTER t20
#endif
cbuffer LXSceneVolumeConstants : register(LX_VOLUME_CONSTANTS_REGISTER)
{
    float4x4 lx_volumeInverseViewProjection;
    float4 lx_volumeEye;
    uint lx_volumeWidth, lx_volumeHeight, lx_volumeFirst, lx_volumeDispatchCount;
    uint lx_volumeTriangleCount, lx_volumeObjectCount, lx_volumeLightCount, lx_volumeEnvironment;
    uint lx_volumeHasShadow;
    float lx_volumeShadowBlend;
    uint2 lx_volumePadding;
    float4x4 lx_volumeShadowProjection[3];
    float4 lx_volumeShadowSplits, lx_volumeShadowBias, lx_volumeCameraForward;
    float4 lx_volumeLights[256];
};
Texture2DArray<float> lx_volumeShadowMap : register(LX_VOLUME_SHADOW_REGISTER);
TextureCube<float4> lx_volumeEnvironmentMap : register(LX_VOLUME_ENVIRONMENT_REGISTER);
#ifndef LX_VOLUME_SHARED_SAMPLERS
SamplerState lx_volumeEnvironmentSampler : register(s2);
SamplerComparisonState lx_volumeShadowSampler : register(s3);
#endif

float LXVolumeShadowCascade(uint cascade, float3 position)
{
    const float4 homogeneous = mul(float4(position, 1), lx_volumeShadowProjection[cascade]);
    const float3 projected = homogeneous.xyz / homogeneous.w;
    if (any(abs(projected.xy) > 1) || projected.z < 0 || projected.z > 1)
        return 1;
    const float2 uv = float2(projected.x * .5 + .5, .5 - projected.y * .5);
    return lx_volumeShadowMap.SampleCmpLevelZero(lx_volumeShadowSampler, float3(uv, cascade),
                                                projected.z - lx_volumeShadowBias[cascade]);
}

float LXVolumeShadow(float3 position)
{
    if (lx_volumeHasShadow == 0)
        return 1;
    const float distance = dot(position - lx_volumeEye.xyz, lx_volumeCameraForward.xyz);
    const uint cascade = distance > lx_volumeShadowSplits.y ? 2 : distance > lx_volumeShadowSplits.x ? 1 : 0;
    float visibility = LXVolumeShadowCascade(cascade, position);
    if (lx_volumeShadowBlend > 0 && cascade < 2)
    {
        const float split = lx_volumeShadowSplits[cascade];
        const float start = split * (1 - lx_volumeShadowBlend);
        if (distance > start)
            visibility = lerp(visibility, LXVolumeShadowCascade(cascade + 1, position),
                               saturate((distance - start) / max(split - start, 1e-4)));
    }
    return visibility;
}

float3 LXVolumeIncident(uint object, float3 position, float3 view)
{
    const float anisotropy = LXVolumeMedium(object).anisotropy;
    float3 source = float3(0);
    [loop]
    for (uint i = 0; i < lx_volumeLightCount; ++i)
    {
        const float4 lightPosition = lx_volumeLights[i * 4];
        const float4 direction = lx_volumeLights[i * 4 + 1];
        const float4 color = lx_volumeLights[i * 4 + 2];
        const float4 falloff = lx_volumeLights[i * 4 + 3];
        const uint type = uint(lightPosition.w);
        float3 toLight;
        float attenuation, limit;
        if (type == 0)
        {
            toLight = -normalize(direction.xyz);
            attenuation = (i + 1u == uint(max(lx_volumeShadowSplits.w, 1.0))) ? LXVolumeShadow(position) : 1.0;
            limit = 1e9;
        }
        else
        {
            const float3 offset = lightPosition.xyz - position;
            limit = length(offset);
            toLight = offset / max(limit, 1e-4);
            attenuation = saturate(1 - limit / max(falloff.w, 1e-4))
                / max(falloff.x + falloff.y * limit + falloff.z * limit * limit, 1e-6);
            if (type == 2)
            {
                const float cosine = dot(-toLight, normalize(direction.xyz));
                const float cutoff = cos(direction.w * .5);
                attenuation *= smoothstep(cutoff, lerp(cutoff, 1.0, .2), cosine);
            }
        }
        source += max(color.rgb * color.a, float3(0)) * attenuation * VolumePhase(dot(-toLight, view), anisotropy)
            * LXVolumeVisibility(position, toLight, limit, lx_volumeTriangleCount, lx_volumeObjectCount);
    }
    if (lx_volumeEnvironment != 0)
    {
        float3 environment = float3(0);
        float normalization = 0;
        [loop]
        for (uint i = 0; i < 32; ++i)
        {
            const float z = 1 - 2 * (float(i) + .5) / 32;
            const float radius = sqrt(max(1 - z * z, 0));
            const float angle = float(i) * 2.39996323;
            const float3 direction = float3(radius * cos(angle), radius * sin(angle), z);
            const float weight = VolumePhase(dot(-direction, view), anisotropy);
            const float3 incoming = max(lx_volumeEnvironmentMap.SampleLevel(lx_volumeEnvironmentSampler,
                                                                           direction, 0).rgb, float3(0));
            environment += incoming * weight * LXVolumeVisibility(position, direction, 1e9,
                                                                   lx_volumeTriangleCount, lx_volumeObjectCount);
            normalization += weight;
        }
        source += environment / max(normalization, 1e-20);
    }
    return source;
}

// Finite single-scattering quadrature: exact homogeneous extinction/emission;
// direct/environment incident transport is sampled at eight points per interval.
VolumeSegment LXSceneVolumeTransport(float3 origin, float3 direction, float limit)
{
    VolumeSegment result;
    result.transmittance = float3(1);
    result.radiance = float3(0);
    if (lx_volumeTriangleCount == 0 || limit <= 0 || dot(direction, direction) < .5)
        return result;
    LXVolumeEvent events[LX_VOLUME_MAX_TRIANGLES];
    int winding[LX_VOLUME_MAX_OBJECTS];
    const uint count = LXVolumeEvents(origin, direction, lx_volumeTriangleCount, events, winding);
    float previous = 0;
    [loop]
    for (uint i = 0; i <= min(count, LX_VOLUME_MAX_TRIANGLES); ++i)
    {
        const float end = i < count ? min(events[i].distance, limit) : limit;
        PrincipledVolume combined;
        combined.scattering = combined.absorption = combined.emission = float3(0);
        combined.anisotropy = 0;
        [loop]
        for (uint object = 0; object < lx_volumeObjectCount; ++object)
        {
            if (winding[object] != 0)
            {
                const PrincipledVolume medium = LXVolumeMedium(object);
                combined.scattering += medium.scattering;
                combined.absorption += medium.absorption;
                combined.emission += medium.emission;
            }
        }
        const float length = max(end - previous, 0);
        if (length > 0)
        {
            if (any(combined.scattering > 0))
            {
                [loop]
                for (uint step = 0; step < 8; ++step)
                {
                    const float3 position = origin + direction * (previous + length * (float(step) + .5) / 8);
                    float3 scatteringSource = float3(0);
                    for (uint object = 0; object < lx_volumeObjectCount; ++object)
                    {
                        if (winding[object] != 0 && any(LXVolumeMedium(object).scattering > 0))
                            scatteringSource += LXVolumeMedium(object).scattering
                                * LXVolumeIncident(object, position, -direction);
                    }
                    const float3 source = scatteringSource / max(combined.scattering, float3(1e-20));
                    result = ComposeVolumeSegments(result, IntegrateVolumeSegment(combined, length / 8, source));
                }
            }
            else
                result = ComposeVolumeSegments(result, IntegrateVolumeSegment(combined, length, float3(0)));
        }
        if (end >= limit)
            break;
        winding[events[i].object] += events[i].crossing;
        previous = end;
    }
    return result;
}
