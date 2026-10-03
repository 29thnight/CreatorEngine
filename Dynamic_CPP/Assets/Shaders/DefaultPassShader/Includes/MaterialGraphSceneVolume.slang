#pragma once
#include "PrincipledVolume.slang"
#include "MaterialGraphSceneVolumeTypes.slang"

static const uint LX_VOLUME_MAX_OBJECTS = 16;
static const uint LX_VOLUME_MAX_TRIANGLES = 128;

struct LXSceneVolumeTriangle
{
    float4 a, b, c;
};
#ifndef LX_VOLUME_TRIANGLES_REGISTER
#define LX_VOLUME_TRIANGLES_REGISTER t17
#define LX_VOLUME_COEFFICIENTS_REGISTER t18
#endif
StructuredBuffer<LXSceneVolumeTriangle> lx_volumeTriangles : register(LX_VOLUME_TRIANGLES_REGISTER);
StructuredBuffer<LXSceneVolumeCoefficient> lx_volumeCoefficients : register(LX_VOLUME_COEFFICIENTS_REGISTER);

struct LXVolumeEvent
{
    float distance;
    uint object;
    int crossing;
};

bool LXVolumeIntersection(float3 origin, float3 direction, LXSceneVolumeTriangle triangle,
                          out float distance, out int crossing)
{
    const float3 ab = triangle.b.xyz - triangle.a.xyz;
    const float3 ac = triangle.c.xyz - triangle.a.xyz;
    const float3 p = cross(direction, ac);
    const float determinant = dot(ab, p);
    distance = 0;
    crossing = 0;
    if (abs(determinant) <= 1e-7 * length(ab) * length(ac))
        return false;
    const float3 offset = origin - triangle.a.xyz;
    const float u = dot(offset, p) / determinant;
    const float3 q = cross(offset, ab);
    const float v = dot(direction, q) / determinant;
    distance = dot(ac, q) / determinant;
    crossing = determinant > 0 ? 1 : -1;
    return u >= -1e-6 && v >= -1e-6 && u + v <= 1.000001 && distance > 0 && isfinite(distance);
}

// All positive intersections, including exits beyond the camera far plane, are
// needed to determine whether the ray origin is already inside each medium.
uint LXVolumeEvents(float3 origin, float3 direction, uint triangles,
                     out LXVolumeEvent events[LX_VOLUME_MAX_TRIANGLES],
                     out int winding[LX_VOLUME_MAX_OBJECTS])
{
    uint count = 0;
    for (uint object = 0; object < LX_VOLUME_MAX_OBJECTS; ++object)
        winding[object] = 0;
    [loop]
    for (uint i = 0; i < min(triangles, LX_VOLUME_MAX_TRIANGLES); ++i)
    {
        const LXSceneVolumeTriangle triangle = lx_volumeTriangles[i];
        float distance;
        int crossing;
        if (LXVolumeIntersection(origin, direction, triangle, distance, crossing))
        {
            LXVolumeEvent event;
            event.distance = distance;
            event.object = uint(triangle.a.w);
            event.crossing = crossing;
            uint insert = count;
            [loop]
            for (uint shift = 0; shift < LX_VOLUME_MAX_TRIANGLES && insert > 0; ++shift)
            {
                if (events[insert - 1].distance <= distance)
                    break;
                events[insert] = events[insert - 1];
                --insert;
            }
            events[insert] = event;
            ++count;
        }
    }
    // Merge coplanar triangle hits at shared edges. Opposing crossings at a
    // tangent cancel; repeated triangles of one oriented boundary cross once.
    uint compact = 0;
    uint first = 0;
    [loop]
    for (uint group = 0; group < LX_VOLUME_MAX_TRIANGLES && first < count; ++group)
    {
        uint last = first + 1;
        const float distance = events[first].distance;
        const float tolerance = 2e-6 * max(1.0, events[first].distance);
        [loop]
        for (; last < count; ++last)
        {
            if (abs(events[last].distance - distance) > tolerance)
                break;
        }
        int crossings[LX_VOLUME_MAX_OBJECTS];
        for (uint object = 0; object < LX_VOLUME_MAX_OBJECTS; ++object)
        {
            int positive = 0, negative = 0;
            for (uint i = first; i < last; ++i)
            {
                if (events[i].object == object)
                {
                    positive |= events[i].crossing > 0 ? 1 : 0;
                    negative |= events[i].crossing < 0 ? 1 : 0;
                }
            }
            crossings[object] = positive - negative;
        }
        for (uint object = 0; object < LX_VOLUME_MAX_OBJECTS; ++object)
        {
            const int crossing = crossings[object];
            if (crossing != 0)
            {
                LXVolumeEvent event;
                event.distance = distance;
                event.object = object;
                event.crossing = crossing;
                events[compact++] = event;
                winding[object] -= crossing;
            }
        }
        first = last;
    }
    return compact;
}

PrincipledVolume LXVolumeMedium(uint object)
{
    const LXSceneVolumeCoefficient source = lx_volumeCoefficients[object];
    PrincipledVolume result;
    result.scattering = source.scatteringAnisotropy.rgb;
    result.absorption = source.absorption.rgb;
    result.emission = source.emission.rgb;
    result.anisotropy = source.scatteringAnisotropy.w;
    return result;
}

// Absorption/scattering extinction along a light ray. The event bound comes
// from CPU admission, so every closed interval is retained for shadow transport.
float3 LXVolumeVisibility(float3 origin, float3 direction, float limit, uint triangles, uint objects)
{
    LXVolumeEvent events[LX_VOLUME_MAX_TRIANGLES];
    int winding[LX_VOLUME_MAX_OBJECTS];
    const uint count = LXVolumeEvents(origin, direction, triangles, events, winding);
    float3 opticalDepth = float3(0);
    float previous = 0;
    [loop]
    for (uint i = 0; i <= min(count, LX_VOLUME_MAX_TRIANGLES); ++i)
    {
        const float end = i < count ? min(events[i].distance, limit) : limit;
        float3 extinction = float3(0);
        for (uint object = 0; object < objects; ++object)
        {
            if (winding[object] != 0)
            {
                const PrincipledVolume medium = LXVolumeMedium(object);
                extinction += medium.scattering + medium.absorption;
            }
        }
        opticalDepth += extinction * max(end - previous, 0);
        if (end >= limit)
            break;
        winding[events[i].object] += events[i].crossing;
        previous = end;
    }
    return exp(-opticalDepth);
}
