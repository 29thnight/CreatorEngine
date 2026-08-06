#include "Sampler.hlsli"

Texture2D SkyboxTexture : register(t0);

struct PixelShaderInput
{
    float4 position : SV_POSITION;
    float3 texCoord : TEXCOORD;
};

// 1/(2*pi), 1/pi. 4자리 근삿값은 4K HDR에서 U를 최대 약 0.7 texel
// 이동시키므로 충분한 정밀도로 고정한다.
static const float2 invAtan = float2(0.15915494309189535, 0.3183098861837907);
float2 SampleSphericalMap(float3 v)
{
    float2 uv = float2(atan2(v.z, v.x), -asin(v.y));
    uv *= invAtan;
    uv += 0.5;
    return uv;
}

float4 main(PixelShaderInput IN) : SV_TARGET
{
    float2 uv = SampleSphericalMap(normalize(IN.texCoord));
    return float4(SkyboxTexture.Sample(LinearSampler, uv).rgb, 1.0);
}
