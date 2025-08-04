#include "Sampler.hlsli"

TextureCube CubeMap : register(t0);

struct PixelShaderInput
{
    float4 position : SV_POSITION;
    float3 texCoord : TEXCOORD;
};

static const float PI = 3.14159265359;
static const uint SAMPLE_COUNT = 1024u;
static const float BLUR_MIP_LEVEL = 7.0f;
static const float HARD_CLAMP = 1.2f; // 최대 RGB 채널 제한
static const float MAX_LUMINANCE = 1.5f; // 전체 밝기 기준 컷오프
static const float LOG_POWER = 0.5f; // log 조정 시 감쇠 비율

float RadicalInverse_VdC(uint bits)
{
    bits = (bits << 16) | (bits >> 16);
    bits = ((bits & 0x55555555) << 1) | ((bits & 0xAAAAAAAA) >> 1);
    bits = ((bits & 0x33333333) << 2) | ((bits & 0xCCCCCCCC) >> 2);
    bits = ((bits & 0x0F0F0F0F) << 4) | ((bits & 0xF0F0F0F0) >> 4);
    bits = ((bits & 0x00FF00FF) << 8) | ((bits & 0xFF00FF00) >> 8);
    return float(bits) * 2.3283064365386963e-10;
}

float2 Hammersley(uint i, uint N)
{
    return float2((float) i / (float) N, RadicalInverse_VdC(i));
}

float3 SampleHemisphereCosine(float2 Xi)
{
    float phi = 2.0 * PI * Xi.x;
    float cosTheta = sqrt(1.0 - Xi.y);
    float sinTheta = sqrt(Xi.y);
    return float3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
}

float Luminance(float3 color)
{
    return dot(color, float3(0.2126f, 0.7152f, 0.0722f));
}

float4 main(PixelShaderInput IN) : SV_TARGET
{
    float3 N            = normalize(IN.texCoord);
    float3 up           = abs(N.y) < 0.999 ? float3(0.0, 1.0, 0.0) : float3(1.0, 0.0, 0.0);
    float3 right        = normalize(cross(up, N));
    float3 irradiance   = float3(0.0, 0.0, 0.0);
    
    up = cross(N, right);
    
    for (uint i = 0; i < SAMPLE_COUNT; ++i)
    {
        float2 Xi       = Hammersley(i, SAMPLE_COUNT);
        float3 localDir = SampleHemisphereCosine(Xi);
        float3 L        = normalize(localDir.x * right + localDir.y * up + localDir.z * N);
        float  NoL      = localDir.z;
        float3 color    = CubeMap.SampleLevel(LinearSampler, L, BLUR_MIP_LEVEL).rgb;

        // 하드 채널 클램프
        color = min(color, HARD_CLAMP);

        // 고휘도 제거 (Luminance 기준)
        if (Luminance(color) > MAX_LUMINANCE)
            color = 0;

        // 로그 스페이스 감쇠
        color = pow(color + 1e-4f, LOG_POWER);

        irradiance += color * NoL;
    }

    irradiance *= (PI / SAMPLE_COUNT);

    // 로그에서 돌아오기
    irradiance = pow(irradiance, 1.0f / LOG_POWER);

    return float4(irradiance, 1.0);
}
