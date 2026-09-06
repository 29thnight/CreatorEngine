#pragma once

static const uint PBR_COVERAGE_ENABLED = 1u;
static const uint PBR_COVERAGE_MASKED = 2u;
static const uint PBR_COVERAGE_BLENDED = 4u;
static const uint PBR_COVERAGE_DOUBLE_SIDED = 8u;

float ApplyPbrCoverage(float alpha, float cutoff, uint flags, bool frontFace)
{
    if ((flags & PBR_COVERAGE_ENABLED) == 0u) return alpha;
    // Per-instance face rejection keeps differently sided instances in one batch.
    // All coverage passes use cull=None so a double-sided back face reaches here.
    if (!frontFace && (flags & PBR_COVERAGE_DOUBLE_SIDED) == 0u) discard;
    if ((flags & PBR_COVERAGE_MASKED) != 0u) clip(alpha - cutoff);
    return (flags & PBR_COVERAGE_BLENDED) != 0u ? alpha : 1.0;
}

float3 OrientPbrNormal(float3 normal, uint flags, bool frontFace)
{
    return !frontFace && (flags & PBR_COVERAGE_ENABLED) != 0u
        && (flags & PBR_COVERAGE_DOUBLE_SIDED) != 0u ? -normal : normal;
}
