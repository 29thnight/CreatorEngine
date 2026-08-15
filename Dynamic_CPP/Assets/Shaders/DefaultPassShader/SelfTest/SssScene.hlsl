#ifdef __spirv__
[[vk::image_format("rgba16f")]]
#endif
RWTexture2D<float4> gColor : register(u0);
RWTexture2D<float>  gDepth : register(u1);

cbuffer SceneParams : register(b0)
{
    uint2 gSize;
    uint gLeftX;
    uint gRightX;
    uint gPointY;
    uint gStepX;
    uint2 gPad;
};

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    if (any(id.xy >= gSize)) return;

    float4 color = 0.0f;
    if (id.y == gPointY && id.x == gLeftX)
        color = float4(1.0f, 0.0f, 0.0f, 1.0f);
    else if (id.y == gPointY && id.x == gRightX)
        color = float4(0.0f, 1.0f, 0.0f, 1.0f);

    gColor[id.xy] = color;
    gDepth[id.xy] = (id.x >= gStepX) ? 0.9f : 0.5f;
}
