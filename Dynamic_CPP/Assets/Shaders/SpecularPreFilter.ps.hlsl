#include "Sampler.hlsli"
#include "BRDF.hlsli"
#include "Shading.hlsli"

static const uint SAMPLE_COUNT      = 1024u;
static const float defaultWeight    = 0.0000001f;
static const float HARD_CLAMP       = 1.2f;
static const float MAX_LUMINANCE    = 1.5f;
static const float LOG_POWER        = 0.5f;

struct PixelShaderInput
{
    float4 position : SV_POSITION;
    float3 texCoord : TEXCOORD;
};

cbuffer SpecularCBuffer : register(b0)
{
    float roughness;
};

TextureCube SkyboxTexture : register(t0);

float Luminance(float3 color)
{
    return dot(color, float3(0.2126f, 0.7152f, 0.0722f));
}

float4 main(PixelShaderInput IN) : SV_TARGET
{
    float3  N                   = normalize(IN.texCoord);
    float3  R                   = N;
    float3  V                   = R;
    float   totalWeight         = defaultWeight;
    float3  prefilteredColor    = float3(0.0, 0.0, 0.0);

    for (uint i = 0u; i < SAMPLE_COUNT; ++i)
    {
        float2 st   = Hammersley(i, SAMPLE_COUNT);
        float3 H    = ImportanceSampleGGX(st, N, roughness);
        float3 L    = normalize(2.0 * dot(V, H) * H - V);

        float NdotL = saturate(dot(N, L));
        if (NdotL > 0.0)
        {
            float3 color = SkyboxTexture.Sample(LinearSampler, L).rgb;

            // 하드 채널 클램프
            color = min(color, HARD_CLAMP);

            // 고휘도 제거
            if (Luminance(color) > MAX_LUMINANCE)
                color = 0;

            // 로그 스페이스 감쇠
            color = pow(color + 1e-4f, LOG_POWER);

            prefilteredColor += color * NdotL;
            totalWeight += NdotL;
        }
    }
    prefilteredColor = prefilteredColor / max(totalWeight, defaultWeight);

    // 로그 복원
    prefilteredColor = pow(prefilteredColor, 1.0f / LOG_POWER);

    return float4(prefilteredColor, 1.0);
}
