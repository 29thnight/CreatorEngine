#pragma once
// The immutable cooker certificate covers full triangle interiors in both
// directions. The pose packet is the same pending owner consumed by skinning.
bool MeteorSkinGroupMetric(StructuredBuffer<float4> pose, ByteAddressBuffer topology, uint node, inout float4 sphere, inout float error)
{
    const uint flags = topology.Load(node + 60u);
    if ((flags & 1u) == 0u)
    {
        return true;
    }
    const uint count = MeteorPoseWord(pose, 0u);
    if (count == 0u)
    {
        return false;
    }
    const bool hasBoneBounds = MeteorPoseWord(pose, 4u) == 0x31424249u &&
        MeteorPoseWord(pose, 12u) == count + 1u;
    const uint boundsOffset = hasBoneBounds ? MeteorPoseWord(pose, 8u) : 0u;
    uint referenceBone = 0u;
    if (hasBoneBounds)
    {
        // The table covers every source vertex, including nonresident pages.
        // An unused palette entry cannot affect either the patch or its error.
        while (referenceBone < count &&
               MeteorPoseWord(pose, boundsOffset + referenceBone * 32u + 12u) == 0u &&
               MeteorPoseWord(pose, boundsOffset + referenceBone * 32u + 28u) == 0u)
        {
            ++referenceBone;
        }
    }
    const float4x4 identity = float4x4(1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1);
    const float4x4 reference = referenceBone < count ? MeteorBone(pose, referenceBone) : identity;
    float referenceNorm = 0.0f, deviation = 0.0f, translation = 0.0f;
    [unroll] for (uint row = 0u; row < 3u; ++row)
    {
        referenceNorm += dot(reference[row].xyz, reference[row].xyz);
    }
    referenceNorm = sqrt(referenceNorm);
    float3 lower = float3(3.402823466e+38f), upper = -lower;
    const uint boundCount = count + ((flags & 2u) != 0u ? 1u : 0u);
    [loop] for (uint bone = 0u; bone < boundCount; ++bone)
    {
        if (hasBoneBounds && bone < count &&
            MeteorPoseWord(pose, boundsOffset + bone * 32u + 12u) == 0u &&
            MeteorPoseWord(pose, boundsOffset + bone * 32u + 28u) == 0u)
        {
            continue;
        }
        const float4x4 matrix = bone < count ? MeteorBone(pose, bone) : identity;
        float difference = 0.0f, norm = 0.0f;
        [unroll] for (uint row = 0u; row < 3u; ++row)
        {
            const float3 delta = matrix[row].xyz - reference[row].xyz;
            difference += dot(delta, delta);
            norm += dot(matrix[row].xyz, matrix[row].xyz);
        }
        deviation = max(deviation, sqrt(difference));
        translation = max(translation, length(matrix[3].xyz - reference[3].xyz));
        const float3 center = mul(float4(sphere.xyz, 1.0f), matrix).xyz;
        const float radius = sphere.w*sqrt(norm);
        lower = min(lower, center - radius);
        upper = max(upper, center + radius);
    }
    const float2 coefficients = asfloat(topology.Load2(node + 52u));
    const float rounding = ((referenceNorm + deviation)*(length(sphere.xyz) + sphere.w) +
        length(reference[3].xyz) + translation + 1.0f)*0.00004f;
    error = (referenceNorm*error + deviation*coefficients.x + translation*coefficients.y + rounding)*1.0001f;
    const float3 extent = (upper-lower)*0.5f;
    sphere = float4((lower+upper)*0.5f, length(extent)*1.0001f + rounding);
    return all(isfinite(sphere)) && isfinite(error) && error >= 0.0f;
}
