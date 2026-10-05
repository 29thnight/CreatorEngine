#pragma once

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

// Snapshot of the completed farther Forward+ objects. The snapshot never reads
// the target being composed, the current transmitting draw, or previous frames.
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
                    float3 color = SanitizeRadiance(lx_background.Load(int3(finalPixel, 0)).rgb);
#ifdef LX_SCENE_REFRACTION_VOLUME
                    // Forward+ backgrounds already contain camera-ray medium.
                    // Remove that segment before integrating the refracted ray.
                    if (lx_reserved != 0u)
                    {
                        const float2 ndc = float2((float(finalPixel.x) + .5) / lx_width * 2 - 1,
                                                  1 - (float(finalPixel.y) + .5) / lx_height * 2);
                        const float4 nearH = mul(float4(ndc, 0, 1), lx_inverseViewProjection);
                        const float4 endH = mul(float4(ndc, lx_depth.Load(int3(finalPixel, 0)), 1), lx_inverseViewProjection);
                        const float3 cameraOrigin = nearH.xyz / nearH.w;
                        const float3 offset = endH.xyz / endH.w - cameraOrigin;
                        const float cameraDistance = max(length(offset), 1e-6);
                        const VolumeSegment cameraMedium = LXSceneVolumeTransport(cameraOrigin, offset / cameraDistance, cameraDistance);
                        color = max(color - cameraMedium.radiance, float3(0)) / max(cameraMedium.transmittance, float3(1e-4));
                    }
#endif
                    return color;
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

