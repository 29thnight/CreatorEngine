TextureCube  gCube    : register(t0);
SamplerState gSampler : register(s0);

cbuffer IblDrawConstants : register(b0)
{
    float4 gForward;
    float4 gRight;
    float4 gUp;
    float4 gParams;   // x = roughness
};

struct VSOut
{
    float4 position : SV_POSITION;
    float3 texCoord : TEXCOORD0;
};

static const float PI = 3.14159265359;
static const uint SAMPLE_COUNT = 1024u;
static const float defaultWeight = 0.0000001f;
static const float HARD_CLAMP = 1.2f;
static const float MAX_LUMINANCE = 1.5f;
static const float LOG_POWER = 0.5f;

float RadicalInverse_VdC(uint bits)
{
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10;
}

float2 Hammersley(uint i, uint N)
{
    return float2(float(i) / float(N), RadicalInverse_VdC(i));
}

float3 ImportanceSampleGGX(float2 st, float3 N, float roughness)
{
    float a = roughness * roughness;

    float phi = 2.0 * PI * st.x;
    float cosTheta = sqrt((1.0 - st.y) / (1.0 + (a * a - 1.0) * st.y));
    float sinTheta = sqrt(1.0 - cosTheta * cosTheta);

    float3 H;
    H.x = cos(phi) * sinTheta;
    H.y = sin(phi) * sinTheta;
    H.z = cosTheta;

    float3 up = abs(N.z) < 0.999 ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0);
    float3 tangent = normalize(cross(up, N));
    float3 bitangent = cross(N, tangent);

    float3 sampleVec = tangent * H.x + bitangent * H.y + N * H.z;
    return normalize(sampleVec);
}

float Luminance(float3 color)
{
    return dot(color, float3(0.2126f, 0.7152f, 0.0722f));
}

float4 PSMain(VSOut input) : SV_TARGET
{
    float3 N = normalize(input.texCoord);
    float3 R = N;
    float3 V = R;
    float totalWeight = defaultWeight;
    float3 prefilteredColor = float3(0.0, 0.0, 0.0);

    for (uint i = 0u; i < SAMPLE_COUNT; ++i)
    {
        float2 st = Hammersley(i, SAMPLE_COUNT);
        float3 H = ImportanceSampleGGX(st, N, gParams.x);
        float3 L = normalize(2.0 * dot(V, H) * H - V);

        float NdotL = saturate(dot(N, L));
        if (NdotL > 0.0)
        {
            float3 color = gCube.Sample(gSampler, L).rgb;

            color = min(color, HARD_CLAMP);

            if (Luminance(color) > MAX_LUMINANCE)
                color = 0;

            color = pow(color + 1e-4f, LOG_POWER);

            prefilteredColor += color * NdotL;
            totalWeight += NdotL;
        }
    }
    prefilteredColor = prefilteredColor / max(totalWeight, defaultWeight);

    prefilteredColor = pow(prefilteredColor, 1.0f / LOG_POWER);

    return float4(prefilteredColor, 1.0);
}
