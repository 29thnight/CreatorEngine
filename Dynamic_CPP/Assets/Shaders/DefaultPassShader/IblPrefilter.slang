#include "Includes/Ibl.slang"

// GGX 프리필터 환경 맵(Karis 2013 split-sum 의 첫째 합). 밉 하나가 거칠기
// 하나이고, 런타임은 mip = roughness * (mipCount - 1) 로 읽는다.
// 생성기가 밉마다 gParams.x 에 그 밉의 거칠기를 싣고 한 번씩 그린다.
//
// ★ 잡음은 환경 큐브의 밉이 잡는다 ─ GGX 표본의 pdf 로 입체각을 재어 그에
//   맞는 밉을 읽는다(SelectEnvironmentMip). 예전에는 누적값을 톤맵 공간에서
//   평균내 눌렀는데, 그 억제는 대비가 있는 환경에서 결과를 어둡게 편향시켰다.

cbuffer IblDrawConstants : register(b0)
{
    float4 gForward;
    float4 gRight;
    float4 gUp;
    float4 gParams;   // x = 거칠기 · z = 환경 큐브 한 변 · w = 환경 큐브 밉 수
};

TextureCube  gCube    : register(t0);
SamplerState gSampler : register(s0);

struct VSOut
{
    float4 position : SV_POSITION;
    float3 texCoord : TEXCOORD0;
};

static const uint kSampleCount = 1024u;

[shader("fragment")]
float4 PSMain(VSOut input) : SV_TARGET
{
    // Karis 의 단순화: V = N = R. 로브의 비축 이방성을 버리는 대신 밉을
    // 반사 방향 하나로 인덱싱할 수 있게 된다.
    const float3 n = normalize(input.texCoord);
    const float3 v = n;

    const float roughness = saturate(gParams.x);
    const float cubeSize  = max(gParams.z, 1.0);
    const float mipCount  = max(gParams.w, 1.0);

    // 거칠기 0 은 거울이다. 로브가 한 방향으로 무너지므로 밉 0 을 그대로
    // 읽어야 반사가 흐려지지 않는다.
    const float alpha = roughness * roughness;

    float3 sum    = float3(0.0, 0.0, 0.0);
    float  weight = 0.0;

    for (uint i = 0u; i < kSampleCount; ++i)
    {
        const float3 h = ImportanceSampleGGX(Hammersley(i, kSampleCount), n, roughness);
        const float3 l = normalize(2.0 * dot(v, h) * h - v);

        const float ndotl = saturate(dot(n, l));
        if (ndotl <= 0.0) continue;

        // GGX 표본 pdf. V = N 이라 NdotH == VdotH 이고 pdf 가 D/4 로 준다.
        const float ndoth = saturate(dot(n, h));
        const float d     = DistributionGGX(ndoth, max(alpha, 1e-4));
        const float pdf   = d * 0.25;

        const float mip = SelectEnvironmentMip(pdf, kSampleCount, cubeSize, mipCount);

        // 표본마다 정제한다 ─ Inf 텍셀 하나가 누적기 전체를 Inf/NaN 으로
        // 만들고 나면 그 면은 되살릴 수 없다.
        sum    += SanitizeRadiance(gCube.SampleLevel(gSampler, l, mip).rgb) * ndotl;
        weight += ndotl;
    }

    const float3 prefiltered = sum / max(weight, 1e-4);
    return float4(SanitizeRadiance(prefiltered), 1.0);
}
