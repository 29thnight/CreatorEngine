// Keep the decoded source in the cook. Mirrors must not traverse a second
// projection/filter footprint through the irradiance integration cube.
Texture2D<float4> gSource : register(t0);
float4 PSMain(float4 position : SV_Position) : SV_Target
{
    return gSource.Load(int3(uint2(position.xy), 0));
}
