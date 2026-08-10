Texture2D    gDepth   : register(t0);
Texture2D    gColor   : register(t1);
SamplerState gSampler : register(s0);

cbuffer SSSConstants : register(b0)
{
    float2 gDirection;
    float  gStrength;
    float  gWidth;
    float  gCameraFov;
    float3 gPadding;
};

struct VSOut
{
    float4 position : SV_POSITION;
    float2 texCoord : TEXCOORD0;
};

VSOut VSMain(uint id : SV_VertexID)
{
    VSOut output;
    output.texCoord = float2((id << 1) & 2, id & 2);
    output.position = float4(output.texCoord * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f),
        0.0f, 1.0f);
    return output;
}

static const int NUM_SAMPLES = 25;

static const float4 kernel[NUM_SAMPLES] =
{
    float4(0.530605,    0.613514,       0.739601,        0),
    float4(0.000973794, 1.11862e-005,   9.43437e-007,   -3),
    float4(0.00333804,  7.85443e-005,   1.2945e-005,    -2.52083),
    float4(0.00500364,  0.00020094,     5.28848e-005,   -2.08333),
    float4(0.00700976,  0.00049366,     0.000151938,    -1.6875),
    float4(0.0094389,   0.00139119,     0.000416598,    -1.33333),
    float4(0.0128496,   0.00356329,     0.00132016,     -1.02083),
    float4(0.017924,    0.00711691,     0.00347194,     -0.75),
    float4(0.0263642,   0.0119715,      0.00684598,     -0.520833),
    float4(0.0410172,   0.0199899,      0.0118481,      -0.333333),
    float4(0.0493588,   0.0367726,      0.0219485,      -0.1875),
    float4(0.0402784,   0.0657244,      0.04631,        -0.0833333),
    float4(0.0211412,   0.0459286,      0.0378196,      -0.0208333),
    float4(0.0211412,   0.0459286,      0.0378196,       0.0208333),
    float4(0.0402784,   0.0657244,      0.04631,         0.0833333),
    float4(0.0493588,   0.0367726,      0.0219485,       0.1875),
    float4(0.0410172,   0.0199899,      0.0118481,       0.333333),
    float4(0.0263642,   0.0119715,      0.00684598,      0.520833),
    float4(0.017924,    0.00711691,     0.00347194,      0.75),
    float4(0.0128496,   0.00356329,     0.00132016,      1.02083),
    float4(0.0094389,   0.00139119,     0.000416598,     1.33333),
    float4(0.00700976,  0.00049366,     0.000151938,     1.6875),
    float4(0.00500364,  0.00020094,     5.28848e-005,    2.08333),
    float4(0.00333804,  7.85443e-005,   1.2945e-005,     2.52083),
    float4(0.000973794, 1.11862e-005,   9.43437e-007,    3)
};

float4 PSMain(VSOut input) : SV_TARGET
{
    const float4 colorM = gColor.SampleLevel(gSampler, input.texCoord, 0);
    const float  depthM = gDepth.SampleLevel(gSampler, input.texCoord, 0).r;

    // 투영창까지의 거리. FOV를 도(degree)로 받는 것까지 원본 그대로다.
    const float distanceToProjectionWindow = 1.0f / tan(0.5f * gCameraFov * 3.141592f / 180.0f);
    const float scale = distanceToProjectionWindow / depthM;

    float2 finalStep = gWidth * scale * gDirection;
    finalStep *= gStrength;
    finalStep *= 0.333f;   // 커널이 -3~3이라 3으로 나눈다

    float4 colorBlurred = colorM;
    colorBlurred.rgb *= kernel[0].rgb;

    [unroll]
    for (int i = 1; i < NUM_SAMPLES; i++)
    {
        const float2 offset = input.texCoord + kernel[i].a * finalStep;
        float4 color = gColor.SampleLevel(gSampler, offset, 0);

        // 표면 추종 — 깊이가 크게 다르면 중심 색으로 되돌린다. 이것이 없으면
        // 실루엣 밖으로 피부색이 번진다.
        {
            const float depth = gDepth.SampleLevel(gSampler, offset, 0).r;
            const float s = saturate(300.0f * distanceToProjectionWindow * gWidth
                * abs(depthM - depth));
            color.rgb = lerp(color.rgb, colorM.rgb, s);
        }

        colorBlurred.rgb += kernel[i].rgb * color.rgb;
    }

    return colorBlurred;
}
