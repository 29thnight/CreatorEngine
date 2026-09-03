#include "Includes/Ibl.slang"

// GGX 프리필터 환경 맵(Karis 2013 split-sum 의 첫째 합). 밉 하나가 거칠기
// 하나이고, 런타임은 mip = roughness * (mipCount - 1) 로 읽는다.
// 생성기가 밉마다 gParams.x 에 그 밉의 거칠기를 싣고 한 번씩 그린다.

cbuffer IblDrawConstants : register(b0)
{
    float4 gForward;
    float4 gRight;
    float4 gUp;
    float4 gParams;   // x = roughness
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

    float3 sum    = float3(0.0);
    float  weight = 0.0;

    for (uint i = 0u; i < kSampleCount; ++i)
    {
        const float3 h = ImportanceSampleGGX(Hammersley(i, kSampleCount), n, roughness);
        const float3 l = normalize(2.0 * dot(v, h) * h - v);

        const float ndotl = saturate(dot(n, l));
        if (ndotl <= 0.0) continue;

        // 표본마다 정제한다 ─ Inf 텍셀 하나가 누적기 전체를 Inf/NaN 으로
        // 만들고 나면 그 면은 되살릴 수 없다.
        const float3 radiance = SanitizeRadiance(
            gCube.SampleLevel(gSampler, l, 0.0).rgb);

        // 톤맵 공간에서 누적한다. 도시 조명·태양처럼 작고 밝은 광원이 표본
        // 몇 개에만 걸리면, 선형 누적에서는 블록 모양 반짝임으로 남는다.
        sum    += FireflyTonemap(radiance) * ndotl;
        weight += ndotl;
    }

    const float3 prefiltered = FireflyUntonemap(sum / max(weight, 1e-4));
    return float4(SanitizeRadiance(prefiltered), 1.0);
}
