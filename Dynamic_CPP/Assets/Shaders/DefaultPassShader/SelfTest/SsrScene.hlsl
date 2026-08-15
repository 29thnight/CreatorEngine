#ifdef __spirv__
[[vk::image_format("rgba16f")]]
#endif
RWTexture2D<float4> gColor : register(u0);
RWTexture2D<float>  gDepth : register(u1);
#ifdef __spirv__
[[vk::image_format("rgba16f")]]
#endif
RWTexture2D<float4> gMetalRough : register(u2);
#ifdef __spirv__
[[vk::image_format("rgba16f")]]
#endif
RWTexture2D<float4> gNormal : register(u3);
RWTexture2D<uint>   gBitmask : register(u4);

cbuffer SceneParams : register(b0)
{
    uint2 gSize;
    uint gGreenStartX;
    uint gMaskMode;
};

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    if (any(id.xy >= gSize)) return;

    const bool green = id.x >= gGreenStartX;
    gColor[id.xy] = float4(0.0f, green ? 1.0f : 0.0f, 0.0f, 1.0f);
    gDepth[id.xy] = 0.5f;
    gMetalRough[id.xy] = float4(
        id.y < gSize.y / 2 ? 1.0f : 0.0f, 0.5f, 0.0f, 1.5f);
    gNormal[id.xy] = float4(
        -0.7247f * 0.5f + 0.5f, 0.5f, 0.6890f * 0.5f + 0.5f, 1.0f);

    const uint terrainBit = 1u << 9;
    const bool corner = all(id.xy == uint2(0, 0));
    if (gMaskMode == 1)
        gBitmask[id.xy] = corner ? terrainBit : 0u;
    else if (gMaskMode == 2)
        gBitmask[id.xy] = corner ? 0u : terrainBit;
    else
        gBitmask[id.xy] = 0u;
}
