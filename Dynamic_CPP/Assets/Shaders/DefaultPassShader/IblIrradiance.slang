#include "Includes/Ibl.slang"

// 확산 조도 컨볼루션. 굽는 값은 E 가 아니라 E/PI 다 ─ Includes/Ibl.slang 의
// "조도 규약" 참조. 런타임은 Irradiance * Albedo 로 곧장 쓴다.
//
// IblFace.slang 의 VS 가 큐브 면 하나를 풀스크린 삼각형으로 덮고 texCoord 에
// 그 픽셀의 월드 방향을 실어 준다. 여기는 그 방향을 법선으로 받는다.
//
// ★ 잡음은 밉이 잡는다 ─ 표본의 입체각에 맞는 환경 밉을 골라 읽는다
//   (SelectEnvironmentMip). 예전에는 큐브에 밉이 없어 누적값을 톤맵 공간에서
//   평균내는 억제를 썼는데, 그 억제는 균일 환경에서만 항등이라 태양이 있는
//   HDRI 에서 조도를 참값의 0.53 배까지 떨어뜨렸다. 이제 적분은 라디언스를
//   그대로 평균낸다.

cbuffer IblDrawConstants : register(b0)
{
    float4 gForward;
    float4 gRight;
    float4 gUp;
    float4 gParams;   // z = 환경 큐브 한 변 · w = 환경 큐브 밉 수
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
    const float3 n = normalize(input.texCoord);

    const float cubeSize = max(gParams.z, 1.0);
    const float mipCount = max(gParams.w, 1.0);

    float3 sum = float3(0.0, 0.0, 0.0);
    for (uint i = 0u; i < kSampleCount; ++i)
    {
        const float3 l = SampleHemisphereCosine(Hammersley(i, kSampleCount), n);

        // 코사인 가중 표본의 pdf 는 cos/PI 다. 스치는 방향일수록 표본이
        // 성겨 한 표본이 넓은 입체각을 맡으므로 더 흐린 밉을 읽는다.
        const float ndotl = saturate(dot(n, l));
        const float mip   = SelectEnvironmentMip(ndotl * INV_PI,
            kSampleCount, cubeSize, mipCount);

        sum += SanitizeRadiance(gCube.SampleLevel(gSampler, l, mip).rgb);
    }

    // 코사인 가중 pdf 가 cos/PI 라 추정자가 평균 하나로 무너진다. 여기서
    // NoL 을 다시 곱하면 integral(L * cos^2) 이 되어 규약이 깨진다.
    const float3 irradiance = sum / float(kSampleCount);
    return float4(SanitizeRadiance(irradiance), 1.0);
}
