#define LX_MATERIAL_FEATURE_MASK 0x3FFFu
#define LX_MATERIAL_ROUTE 2
#include "Includes/PrincipledIblLookup.slang"
#include "Includes/SpecialForwardSurface.slang"
#include "Includes/MaterialGraphSceneRefraction.slang"
#ifdef LX_SCENE_REFRACTION_VOLUME
#include "Includes/MaterialGraphSceneVolumeLighting.slang"
#endif

cbuffer LXSceneRefractionConstants : register(b0)
{
    float4x4 lx_viewProjection, lx_inverseViewProjection;
    uint lx_width, lx_height, lx_first, lx_dispatchCount;
    uint lx_environment, lx_steps, lx_rays, lx_reserved;
};
Texture2D<float4> lx_inputs_0 : register(t0);
Texture2D<float4> lx_inputs_1 : register(t1);
Texture2D<float4> lx_inputs_2 : register(t2);
Texture2D<float4> lx_inputs_3 : register(t3);
Texture2D<float4> lx_inputs_4 : register(t4);
Texture2D<float4> lx_inputs_5 : register(t5);
Texture2D<float4> lx_inputs_6 : register(t6);
Texture2D<float4> lx_inputs_7 : register(t7);
Texture2D<float4> lx_inputs_8 : register(t8);
Texture2D<float4> lx_inputs_9 : register(t9);
Texture2D<float4> lx_inputs_10 : register(t10);

float4 LXLoadSceneInput(uint field, int3 pixel)
{
    switch (field)
    {
        case 0u:
            return lx_inputs_0.Load(pixel);
        case 1u:
            return lx_inputs_1.Load(pixel);
        case 2u:
            return lx_inputs_2.Load(pixel);
        case 3u:
            return lx_inputs_3.Load(pixel);
        case 4u:
            return lx_inputs_4.Load(pixel);
        case 5u:
            return lx_inputs_5.Load(pixel);
        case 6u:
            return lx_inputs_6.Load(pixel);
        case 7u:
            return lx_inputs_7.Load(pixel);
        case 8u:
            return lx_inputs_8.Load(pixel);
        case 9u:
            return lx_inputs_9.Load(pixel);
        case 10u:
            return lx_inputs_10.Load(pixel);
        default:
            return float4(0);
    }
}
Texture2D<float4> lx_positionIor : register(t11);
Texture2D<float4> lx_normalFilmIor : register(t12);
Texture2D<float4> lx_background : register(t13);
Texture2D<float> lx_depth : register(t14);
Texture2D<uint> lx_owners : register(t15);
TextureCube<float4> gEnvironment : register(t16);
SamplerState gEnvironmentSampler : register(s0);
RWStructuredBuffer<LXSceneRefractionSample> lx_samples : register(u0);
#include "Includes/PrincipledEnvironmentBake.slang"

bool LXRefractionProject(float3 position, out float3 screen)
{
    const float4 clip = mul(float4(position, 1), lx_viewProjection);
    screen = float3(0);
    if (clip.w <= 1e-6 || !all(isfinite(clip)))
    {
        return false;
    }
    const float3 ndc = clip.xyz / clip.w;
    screen = float3(ndc.x * .5 + .5, .5 - ndc.y * .5, ndc.z);
    return all(screen.xy >= 0) && all(screen.xy < 1) && screen.z >= 0 && screen.z <= 1;
}

// Finite visible-screen transport against the completed opaque Scene. No reads
// from the target being composed, previous frames, or transmitting surfaces.
float3 LXRefractionMedium(float3 origin, float3 direction, float distance, float3 background)
{
#ifdef LX_SCENE_REFRACTION_VOLUME
    const VolumeSegment medium = LXSceneVolumeTransport(origin, direction, distance);
    return medium.radiance + medium.transmittance * background;
#else
    return background;
#endif
}

float3 LXRefractionBackground(float3 origin, float3 direction, out float hit, out float transportDistance)
{
    hit = 0;
    transportDistance = 1e9;
    if (dot(direction, direction) <= 1e-12)
    {
        return float3(0);
    }
    float3 start;
    if (LXRefractionProject(origin, start))
    {
        const float4 farClip = float4(start.x * 2 - 1, 1 - start.y * 2, .99999, 1);
        const float4 farPoint = mul(farClip, lx_inverseViewProjection);
        if (abs(farPoint.w) > 1e-6 && all(isfinite(farPoint)))
        {
            const float distance = max(length(farPoint.xyz / farPoint.w - origin) * 2, 1e-4);
            float previousGap = -1;
            for (uint step = 1; step <= lx_steps; ++step)
            {
                float3 screen;
                if (!LXRefractionProject(origin + direction * (distance * float(step) / lx_steps), screen))
                {
                    break;
                }
                const int2 pixel = int2(screen.xy * float2(lx_width, lx_height));
                const float backgroundDepth = lx_depth.Load(int3(pixel, 0));
                // A foreground silhouette cannot become a refracted backdrop.
                if (backgroundDepth <= start.z + 1e-5)
                {
                    break;
                }
                const float gap = screen.z - backgroundDepth;
                if (backgroundDepth < .99999 && previousGap < 0 && gap >= 0)
                {
                    float lower = distance * float(step - 1) / lx_steps;
                    float upper = distance * float(step) / lx_steps;
                    for (uint refine = 0; refine < 8; ++refine)
                    {
                        const float middle = (lower + upper) * .5;
                        float3 candidate;
                        if (!LXRefractionProject(origin + direction * middle, candidate))
                        {
                            break;
                        }
                        const int2 candidatePixel = int2(candidate.xy * float2(lx_width, lx_height));
                        const float candidateDepth = lx_depth.Load(int3(candidatePixel, 0));
                        if (candidateDepth <= start.z + 1e-5)
                        {
                            return lx_environment != 0 ? BakeRadiance(direction) : float3(0);
                        }
                        if (candidate.z >= candidateDepth)
                        {
                            upper = middle;
                            screen = candidate;
                        }
                        else
                        {
                            lower = middle;
                        }
                    }
                    hit = 1;
                    const int2 finalPixel = int2(screen.xy * float2(lx_width, lx_height));
                    if (lx_depth.Load(int3(finalPixel, 0)) <= start.z + 1e-5)
                    {
                        hit = 0;
                        return lx_environment != 0 ? BakeRadiance(direction) : float3(0);
                    }
                    transportDistance = upper;
                    return SanitizeRadiance(lx_background.Load(int3(finalPixel, 0)).rgb);
                }
                previousGap = gap;
            }
        }
    }
    return lx_environment != 0 ? BakeRadiance(direction) : float3(0);
}

float3 LXRefractionTrace(float3 origin, float3 direction, out float hit)
{
    float distance;
    const float3 background = LXRefractionBackground(origin, direction, hit, distance);
    return LXRefractionMedium(origin, direction, distance, background);
}

float3 LXRefractionTransport(PrincipledSurface glass, float3 view, float3 position, out float hitFraction)
{
    hitFraction = 0;
    if (dot(glass.normal, view) <= 0)
    {
        return float3(0);
    }
    if (glass.ior == 1 || glass.roughness == 0)
    {
        return LXRefractionTrace(position, refract(-view, glass.normal, rcp(glass.ior)), hitFraction);
    }
    const float alpha = max(glass.roughness * glass.roughness, 1e-3);
    const float3 tangent = PrincipledProjectedTangent(glass.tangent, glass.normal);
    const float3 bitangent = cross(glass.normal, tangent);
    const PrincipledReflection reflection = BuildPrincipledReflection(glass);
    float3 numerator = float3(0), denominator = float3(0);
    for (uint ray = 0; ray < lx_rays; ++ray)
    {
        const float3 halfVector = PrincipledSampleAnisotropic(Hammersley(ray, lx_rays), glass.normal,
            tangent, bitangent, float2(alpha));
        const float vh = dot(view, halfVector);
        const float3 direction = refract(-view, halfVector, rcp(glass.ior));
        const float nl = -dot(glass.normal, direction);
        if (vh <= 0 || nl <= 0)
        {
            continue;
        }
        const float weight = 4 * nl * TransmissionVisibility(view, direction, glass) * vh
            / max(dot(glass.normal, halfVector), 1e-6);
        const float3 response = (1 - EvaluatePrincipledFresnel(reflection, vh)) * weight;
        float hit;
        numerator += response * LXRefractionTrace(position, direction, hit);
        denominator += response;
        hitFraction += hit / lx_rays;
    }
    return select(denominator > 0, numerator / max(denominator, 1e-20), float3(0));
}

[shader("compute")]
[numthreads(32, 1, 1)]
void LXSceneRefractionBake(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= lx_dispatchCount)
    {
        return;
    }
    const uint index = lx_first + id.x;
    const int2 pixel = int2(index % lx_width, index / lx_width);
    LXSceneRefractionSample sample = (LXSceneRefractionSample)0;
    const float4 positionIor = lx_positionIor.Load(int3(pixel, 0));
    if ((lx_owners.Load(int3(pixel, 0)) & 0x80000000u) == 0 || positionIor.w <= 0)
    {
        lx_samples[index] = sample;
        return;
    }
    PrincipledIblBakePoint point;
    point.baseAlpha = lx_inputs_0.Load(int3(pixel, 0));
    point.normalRoughness = lx_inputs_1.Load(int3(pixel, 0));
    point.metalIorLevelAo = lx_inputs_2.Load(int3(pixel, 0));
    point.tintAnisotropy = lx_inputs_3.Load(int3(pixel, 0));
    point.emissionColorStrength = lx_inputs_4.Load(int3(pixel, 0));
    point.coatWeightRoughIorFilmThickness = lx_inputs_5.Load(int3(pixel, 0));
    point.coatTintFilmIor = lx_inputs_6.Load(int3(pixel, 0));
    point.coatNormalSheenWeight = lx_inputs_7.Load(int3(pixel, 0));
    point.sheenTintRoughness = lx_inputs_8.Load(int3(pixel, 0));
    point.tangentRotation = lx_inputs_9.Load(int3(pixel, 0));
    point.viewTier = lx_inputs_10.Load(int3(pixel, 0));
    if (!IsValidPrincipledIblBakePoint(point))
    {
        lx_samples[index] = sample;
        return;
    }
    PrincipledSurface material = EvaluateIblBakePoint(point);
    const float4 normalFilm = lx_normalFilmIor.Load(int3(pixel, 0));
    material.normal = normalFilm.xyz;
    material.transmissionIor = positionIor.w;
    material.transmissionFilmIor = normalFilm.w;
    material.transmissionF0 = saturate(pow((positionIor.w - 1) / (positionIor.w + 1), 2)
        * max(point.tintAnisotropy.rgb, 0));
    const PrincipledSurface glass = SpecialGlass(material);
    const float3 view = normalize(point.viewTier.xyz);
    const PrincipledIblIntegral integral = IntegrateLayeredReflection(glass, view, false);
    sample.glassSingleAlbedo = float4(integral.singleScatter, integral.directionalAlbedo);
    sample.glassAverage = float4(integral.averageFresnel, 1);
    sample.rawTransmission = float4(IntegrateSpecialTransmission(glass, view), 0);
    if (lx_environment != 0)
    {
        sample.glassEnvironment = float4(BakeReflection(glass, view, true, false), 0);
    }
    float hitFraction;
    sample.radiance.rgb = LXRefractionTransport(glass, view, positionIor.xyz, hitFraction);
    sample.radiance.w = hitFraction;
    lx_samples[index] = sample;
}
