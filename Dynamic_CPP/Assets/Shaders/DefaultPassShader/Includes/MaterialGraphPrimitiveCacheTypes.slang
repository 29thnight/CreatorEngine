#pragma once

#include "MaterialGraphSurfacePoint.slang"
#include "MaterialGraphSceneRecord.slang"
#include "MaterialEvaluation.slang"

static const uint LXPrimitiveCacheCapacity = 16384u;
static const uint LXPrimitiveCacheProbeCount = 8u;
static const uint LXPrimitiveCacheMiss = 0xffffffffu;

struct LXPrimitiveCacheVertex
{
    LXSurfacePoint point;
    float4 rasterClip;
    float4 previousPosition;
};

// ScenePrimitiveCacheContract::kTriangleBytes와 같은 336-byte structured stride.
struct LXPrimitiveCacheTriangle
{
    LXPrimitiveCacheVertex a;
    LXPrimitiveCacheVertex b;
    LXPrimitiveCacheVertex c;
};

uint LXPrimitiveCacheHash(uint address)
{
    address ^= address >> 16u;
    address *= 0x7feb352du;
    address ^= address >> 15u;
    address *= 0x846ca68bu;
    return address ^ (address >> 16u);
}

uint LXPrimitiveCacheFind(StructuredBuffer<uint> keys, uint address)
{
    uint capacity, stride;
    keys.GetDimensions(capacity, stride);
    // 비활성 테이블은 기존 zero-default buffer의 한 원소를 바인딩한다.
    if (capacity != LXPrimitiveCacheCapacity || address == 0u)
    {
        return LXPrimitiveCacheMiss;
    }
    const uint first = LXPrimitiveCacheHash(address);
    [loop]
    for (uint probe = 0u; probe < LXPrimitiveCacheProbeCount; ++probe)
    {
        const uint slot = (first + probe) & (LXPrimitiveCacheCapacity - 1u);
        const uint found = keys[slot];
        if (found == address)
        {
            return slot;
        }
        if (found == 0u)
        {
            break;
        }
    }
    return LXPrimitiveCacheMiss;
}

float4x4 LXPrimitiveWorld(LXSceneGpuRecord record, bool previous)
{
    const float4 c0 = previous ? record.previousWorldColumns[0] : record.worldColumns[0];
    const float4 c1 = previous ? record.previousWorldColumns[1] : record.worldColumns[1];
    const float4 c2 = previous ? record.previousWorldColumns[2] : record.worldColumns[2];
    return transpose(float4x4(c0, c1, c2, float4(0, 0, 0, 1)));
}

LXSurfacePoint LXPrimitiveWorldPoint(LXSurfacePoint source, float4x4 world)
{
    LXSurfacePoint result = source;
    result.position = float4(mul(float4(source.position.xyz, 1), world).xyz, 0);
    float3 n, t, b;
    TransformPbrFrame(source.normal.xyz, source.tangent.xyz, source.bitangent.xyz, (float3x3)world, n, t, b);
    result.normal = float4(n, 0);
    result.tangent = float4(t, 0);
    result.bitangent = float4(b, 0);
    return result;
}
