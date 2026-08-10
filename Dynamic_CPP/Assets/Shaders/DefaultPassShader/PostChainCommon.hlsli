cbuffer PostParams : register(b0)
{
    uint2  gSrcSize;
    uint2  gDstSize;

    float  gBloomThreshold;
    float  gBloomKnee;
    float  gBloomIntensity;
    float  gExposure;

    float  gVignetteRadius;
    float  gVignetteSoftness;
    float  gSaturation;
    float  gContrast;

    float  gFxaaBias;
    float  gFxaaBiasMin;
    float  gFxaaSpanMax;
    uint   gFlags;        // 1 블룸 · 2 톤맵 · 4 비네트 · 8 그레이딩

    float  gVignetteIntensity;   // 감광 상한(0=무효과, 1=기존 완전 감광)
    float3 gPostPadding;         // CPU PostParams 꼬리 패딩과 짝
};

// 선형 샘플러. FXAA가 소수 좌표를 읽어야 해서 필요하다 —
// 자세한 사연은 아래 FXAA 주석에 있다.
SamplerState gLinear : register(s0);

static const uint kFlagBloom    = 1u;
static const uint kFlagToneMap  = 2u;
static const uint kFlagVignette = 4u;
static const uint kFlagGrading  = 8u;
static const uint kFlagAgX      = 16u;   // 없으면 ACES

// 화면 밝기. 톤맵·블룸 임계·FXAA가 모두 같은 정의를 써야 한다 —
// 다르면 '임계는 넘겼는데 톤맵은 안 넘긴' 구간이 생겨 경계가 튄다.
float Luminance(float3 color)
{
    return dot(color, float3(0.2126f, 0.7152f, 0.0722f));
}

// 정수 좌표를 원본 좌표로 옮긴다. 크기가 다른 두 텍스처를 오갈 때
// 0.5 픽셀 중심을 빼먹으면 반 픽셀씩 밀리는데, 그 증상은 '조금 흐리다'로만
// 보여 원인을 짚기 어렵다.
float2 DstToSrcUV(uint2 dst)
{
    return (float2(dst) + 0.5f) / float2(gDstSize);
}

float4 LoadSrc(Texture2D<float4> tex, float2 uv)
{
    const int2 coord = clamp(int2(uv * float2(gSrcSize)),
        int2(0, 0), int2(gSrcSize) - 1);
    return tex.Load(int3(coord, 0));
}
