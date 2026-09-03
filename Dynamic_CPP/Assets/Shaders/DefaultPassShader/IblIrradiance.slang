#include "Includes/Ibl.slang"

// 확산 조도 컨볼루션. 굽는 값은 E 가 아니라 E/PI 다 ─ Includes/Ibl.slang 의
// "조도 규약" 참조. 런타임은 Irradiance * Albedo 로 곧장 쓴다.
//
// IblFace.hlsl 의 VS 가 큐브 면 하나를 풀스크린 삼각형으로 덮고 texCoord 에
// 그 픽셀의 월드 방향을 실어 준다. 여기는 그 방향을 법선으로 받는다.

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

    float3 sum = float3(0.0);
    for (uint i = 0u; i < kSampleCount; ++i)
    {
        const float3 l = SampleHemisphereCosine(Hammersley(i, kSampleCount), n);

        // ★ 밉 0 을 그대로 읽는다. 예전 코드는 밉 7 을 요구했는데 환경
        //   큐브는 밉이 한 장뿐이라 조용히 0 으로 클램프되고 있었다 ─
        //   의도한 사전 블러는 처음부터 일어난 적이 없다. 이제는 사전 블러
        //   대신 파이어플라이 톤맵이 잡음을 맡는다(가역이라 색이 안 죽는다).
        const float3 radiance = SanitizeRadiance(
            gCube.SampleLevel(gSampler, l, 0.0).rgb);

        sum += FireflyTonemap(radiance);
    }

    // 코사인 가중 pdf 가 cos/PI 라 추정자가 평균 하나로 무너진다. 여기서
    // NoL 을 다시 곱하면 integral(L * cos^2) 이 되어 규약이 깨진다.
    const float3 irradiance = FireflyUntonemap(sum / float(kSampleCount));
    return float4(SanitizeRadiance(irradiance), 1.0);
}
