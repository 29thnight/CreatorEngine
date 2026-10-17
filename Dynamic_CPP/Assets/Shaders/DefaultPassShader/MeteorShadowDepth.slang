// Standard-Z cache composition. Hardware Less keeps any nearer existing depth.
Texture2D<float> staticDepth : register(t0);

struct ShadowDepthVertex
{
    float4 position : SV_Position;
};

[shader("vertex")]
ShadowDepthVertex ShadowDepthVS(uint index : SV_VertexID)
{
    ShadowDepthVertex vertex;
    vertex.position = float4(index == 2u ? 3.0 : -1.0, index == 1u ? 3.0 : -1.0, 0.0, 1.0);
    return vertex;
}

[shader("fragment")]
float ShadowDepthPS(ShadowDepthVertex input) : SV_Depth
{
    return staticDepth.Load(int3(uint2(input.position.xy), 0));
}
