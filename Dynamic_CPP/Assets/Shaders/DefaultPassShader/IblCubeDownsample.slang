#include "Includes/Ibl.slang"

// 환경 큐브맵의 밉 한 장을 굽는다. 조도·프리필터가 표본의 입체각에 맞는
// 밉을 고를 수 있으려면 큐브에 밉 체인이 있어야 한다(Karis 2013).
//
// ★ 이 패스가 생긴 이유 ─ 예전에는 큐브가 밉 한 장뿐이라 사전 필터를 쓸
//   길이 없었고, 그래서 표본 잡음을 누적값 억제로 눌렀다. 그 억제는 균일
//   환경에서만 항등이고 대비가 있으면 어두운 쪽으로 크게 편향된다 ― 태양이
//   있는 HDRI 에서 조도가 참값의 0.53 배까지 떨어졌다. 잡음은 여기서 밉이
//   맡고, 적분은 에너지를 그대로 둔다.
//
// ★ 밉 m 을 밉 m-1 이 아니라 **소스 큐브(밉 0)** 에서 직접 굽는다. 같은
//   리소스의 한 밉을 읽으며 다른 밉에 쓰려면 서브리소스 단위 배리어가
//   필요한데 RHITransition 은 텍스처 전체만 전이한다. 소스를 따로 두면
//   전이가 한 번으로 끝나고, 표본 수도 밉 6 에서 4096 뿐이라 값싸다.
//
// 박스 필터를 면 접선 축으로 편다. 면 경계 밖으로 나간 표본은 하드웨어
// 큐브 주소 지정이 이웃 면 텍셀로 풀어 주므로 이음매가 생기지 않는다.

cbuffer IblDrawConstants : register(b0)
{
    float4 gForward;
    float4 gRight;
    float4 gUp;
    float4 gParams;   // y = 이 밉이 덮는 소스 텍셀 폭(2^m) · z = 그리는 밉의 한 변
};

TextureCube  gCube    : register(t0);
SamplerState gSampler : register(s0);

struct VSOut
{
    float4 position : SV_POSITION;
    float3 texCoord : TEXCOORD0;
};

[shader("fragment")]
float4 PSMain(VSOut input) : SV_TARGET
{
    const uint  span       = (uint)max(gParams.y, 1.0);
    const float targetSize = max(gParams.z, 1.0);

    // 면 좌표는 u,v 가 -1..1 이다. 그리는 밉의 텍셀 하나가 면 좌표에서
    // 2/targetSize 폭을 차지하고, 그 안을 span x span 으로 고르게 나눈다.
    const float texelExtent = 2.0 / targetSize;

    float3 sum = float3(0.0, 0.0, 0.0);
    for (uint j = 0u; j < span; ++j)
    {
        const float v = (((float)j + 0.5) / (float)span - 0.5) * texelExtent;
        for (uint i = 0u; i < span; ++i)
        {
            const float u = (((float)i + 0.5) / (float)span - 0.5) * texelExtent;

            const float3 direction = normalize(
                input.texCoord + gRight.xyz * u + gUp.xyz * v);
            sum += SanitizeRadiance(
                gCube.SampleLevel(gSampler, direction, 0.0).rgb);
        }
    }

    return float4(SanitizeRadiance(sum / (float)(span * span)), 1.0);
}
