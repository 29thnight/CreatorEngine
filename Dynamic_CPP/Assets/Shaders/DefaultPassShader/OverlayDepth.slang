cbuffer OverlayDepthParams : register(b0)
{
    float2 displayExtent;
    float2 rasterJitter;
};
Texture2D<float> sceneDepth : register(t0);

float4 VSMain(uint vertexId : SV_VertexID) : SV_POSITION
{
    float2 uv = float2((vertexId << 1u) & 2u, vertexId & 2u);
    return float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);
}

float PSMain(float4 position : SV_POSITION) : SV_DEPTH
{
    uint width, height;
    sceneDepth.GetDimensions(width, height);
    // Raster projection adds a pixel jitter; reconstructed color is stable.
    int2 pixel = int2(floor(position.xy * float2(width, height) / displayExtent + rasterJitter));
    pixel = clamp(pixel, int2(0, 0), int2(width - 1, height - 1));
    return sceneDepth.Load(int3(pixel, 0));
}
