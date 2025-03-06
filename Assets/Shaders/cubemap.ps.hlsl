
struct VertexIn
{
    float3 PosL : POSITION;
};

SamplerState CubemapSampler : register(s0);
TextureCube Cubemap	: register(t0);

float4 ToneMap(float4 colorin)
{
    const float gamma = 1.0f / 2.2f;
	float4 mapped = colorin / (colorin + float4(1.0f,1.0f,1.0f,1.0f));

    mapped.w = 1.0f;
    return mapped;
}

float4 main(VertexIn pin) : SV_Target
{
    return ToneMap(Cubemap.Sample(CubemapSampler, pin.PosL));
}