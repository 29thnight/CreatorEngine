#pragma once
#include "TextureCoordinates.slang"
// Matches MaterialTextureTable::GpuCoordinates and the reflected t16..t127 table.
cbuffer MaterialCoordinates : register(b3)
{
    float4 gMaterialCoordinates[224];
};
float2 MaterialUv(uint textureRegister, float2 uv0, float2 uv1)
{
    const uint index = (textureRegister - 16) * 2;
    TextureCoordinates coordinates;
    coordinates.u = gMaterialCoordinates[index];
    coordinates.v = gMaterialCoordinates[index + 1];
    return TransformTextureUv(coordinates, uv0, uv1);
}
