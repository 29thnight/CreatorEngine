cbuffer DepthFixture : register(b0) { uint width; uint height; uint mode; float depth; };
float4 DepthVS(uint id : SV_VertexID) : SV_Position
{
    return float4(id == 2 ? 3.0 : -1.0, id == 1 ? 3.0 : -1.0, 0, 1);
}
float DepthPS(float4 position : SV_Position) : SV_Depth
{
    uint2 pixel = uint2(position.xy);
    if (mode == 1 && all(pixel == uint2(width / 2, height / 2))) return 1;
    if (mode == 2 && all(pixel == uint2(width - 1, height - 1))) return 1;
    return depth;
}
