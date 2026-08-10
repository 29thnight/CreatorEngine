Texture2D    gEquirect : register(t0);
SamplerState gSampler  : register(s0);

struct VSOut
{
    float4 position : SV_POSITION;
    float3 texCoord : TEXCOORD0;
};

static const float2 invAtan = float2(0.15915494309189535, 0.3183098861837907);
float2 SampleSphericalMap(float3 v)
{
    float2 uv = float2(atan2(v.z, v.x), -asin(v.y));
    uv *= invAtan;
    uv += 0.5;
    return uv;
}

float4 PSMain(VSOut input) : SV_TARGET
{
    float2 uv = SampleSphericalMap(normalize(input.texCoord));
    return float4(gEquirect.Sample(gSampler, uv).rgb, 1.0);
}
