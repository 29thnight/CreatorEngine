#include "Includes/Ibl.slang"

// split-sum 의 둘째 합을 (NdotV, roughness) 격자에 굽는다. 런타임 스페큘러는
// Prefiltered * (F0 * scale + bias) 로 줄어든다.
//
// 적분 자체는 Includes/Ibl.slang 이 소유한다 ─ 프리필터가 쓰는 GGX 표본기와
// 같은 규약(k = a^2/2)을 써야 두 결과가 맞물린다.

struct VSOut
{
    float4 position : SV_POSITION;
    float2 texCoord : TEXCOORD0;
};

[shader("fragment")]
float4 PSMain(VSOut input) : SV_TARGET
{
    // 하한은 참된 0 만 막는다. 텍셀 중심이 (i+0.5)/size 라 실제로는 0 이
    // 들어오지 않지만, 들어오면 G_Vis 의 분모가 0 이 되어 LUT 첫 열이
    // 통째로 Inf 가 된다.
    const float ndotv     = max(input.texCoord.x, 1e-4);
    const float roughness = max(input.texCoord.y, 1e-4);

    // 타깃이 RGBA16F 다. float2 만 반환하면 나머지 채널이 미정의라는 D3D12
    // 검증 경고가 나오므로 BA 를 명시해 채운다 — 소비자는 RG 만 읽는다.
    return float4(IntegrateBrdf(ndotv, roughness), 0.0, 1.0);
}
