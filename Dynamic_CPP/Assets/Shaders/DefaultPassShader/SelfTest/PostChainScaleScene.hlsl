RWTexture2D<float4> gColor : register(u0);

cbuffer SceneParams : register(b0)
{
    uint2 gSize;
    uint2 gPad;
};

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    if (any(id.xy >= gSize)) return;

    float3 color = 0.25f;

    const int2 center = int2(gSize) / 2;
    const int2 delta = abs(int2(id.xy) - center);
    if (delta.x < int(gSize.x) / 16 && delta.y < int(gSize.y) / 16)
    {
        color = 3.0f;
    }

    gColor[id.xy] = float4(color, 1.0f);
}
