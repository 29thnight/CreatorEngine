#pragma once

#include "Brdf.slang"
#include "Radiance.slang"

// Karis 2013 split-sum IBL. 굽는 쪽(IblIrradiance/IblPrefilter/IblBrdf)과
// 소비하는 쪽(Deferred)이 이 한 파일에서 같은 정의를 가져간다.
//
// ── 조도 규약 (중요) ─────────────────────────────────────────────────
//
// IblIrradiance 가 굽는 값은 E 가 아니라 **E/PI**, 즉 코사인 가중 표본의
// 산술 평균이다. Malley 법으로 뽑은 방향의 pdf 가 cos(theta)/PI 이므로
// 추정자가 mean(L) 하나로 무너진다. 그래서 런타임 확산은
//
//     Diffuse = Irradiance * Albedo          (1/PI 를 다시 나누지 않는다)
//
// 다. 예전 코드는 cos 를 pdf 와 별도로 **한 번 더** 곱하고 PI/N 을 곱해
// 실제로는 integral(L * cos^2) 을 구웠고, 소비 쪽도 1/PI 를 빠뜨려 두
// 규약이 서로 맞지 않았다. 굽기와 소비를 같이 고쳐야 하는 이유다.

float RadicalInverseVdC(uint bits)
{
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10;
}

float2 Hammersley(uint index, uint count)
{
    return float2(float(index) / float(count), RadicalInverseVdC(index));
}

// N 주위의 접선 좌표계. N 이 +Z 에 가까울 때 cross 가 퇴화하지 않도록
// up 축을 갈아탄다. 조도·프리필터·LUT 가 같은 규칙을 써야 세 결과가
// 같은 방향 규약 위에 선다.
float3x3 BuildSampleFrame(float3 n)
{
    const float3 up        = abs(n.z) < 0.999 ? float3(0.0, 0.0, 1.0)
                                              : float3(1.0, 0.0, 0.0);
    const float3 tangent   = normalize(cross(up, n));
    const float3 bitangent = cross(n, tangent);
    return float3x3(tangent, bitangent, n);
}

// GGX NDF 중요도 표본. 접선 공간에서 만들어 프레임으로 옮긴다.
float3 ImportanceSampleGGX(float2 xi, float3 n, float roughness)
{
    const float a = roughness * roughness;

    const float phi      = 2.0 * PI * xi.x;
    const float cosTheta = sqrt((1.0 - xi.y) / (1.0 + (a * a - 1.0) * xi.y));
    const float sinTheta = sqrt(1.0 - cosTheta * cosTheta);

    const float3 h = float3(sinTheta * cos(phi), sinTheta * sin(phi), cosTheta);
    const float3x3 frame = BuildSampleFrame(n);
    return normalize(h.x * frame[0] + h.y * frame[1] + h.z * frame[2]);
}

// Malley 법: 단위 원판의 균등 표본을 반구로 올리면 코사인 가중 방향이 된다.
float3 SampleHemisphereCosine(float2 xi, float3 n)
{
    const float phi      = 2.0 * PI * xi.x;
    const float cosTheta = sqrt(1.0 - xi.y);
    const float sinTheta = sqrt(xi.y);

    const float3 local = float3(sinTheta * cos(phi), sinTheta * sin(phi), cosTheta);
    const float3x3 frame = BuildSampleFrame(n);
    return normalize(local.x * frame[0] + local.y * frame[1] + local.z * frame[2]);
}

// split-sum 의 둘째 합을 (scale, bias) 로. 런타임 스페큘러는
// Prefiltered * (F0 * scale + bias) 로 줄어든다.
float2 IntegrateBrdf(float ndotv, float roughness)
{
    // N = +Z 인 접선 공간에서 V 를 재구성한다. 적분에는 NdotV 만 들어가므로
    // z 성분만 맞으면 어떤 V 든 같은 답이 나온다.
    const float3 v = float3(sqrt(1.0 - ndotv * ndotv), 0.0, ndotv);
    const float3 n = float3(0.0, 0.0, 1.0);

    const uint sampleCount = 1024u;

    // Smith 의 시선 항은 표본과 무관하므로 루프 밖으로 뺀다.
    const float gv = GeometrySchlickGgxIbl(ndotv, roughness);

    float scale = 0.0;
    float bias  = 0.0;

    for (uint i = 0u; i < sampleCount; ++i)
    {
        const float3 h = ImportanceSampleGGX(Hammersley(i, sampleCount), n, roughness);
        const float3 l = normalize(2.0 * dot(v, h) * h - v);

        const float ndotl = saturate(l.z);
        if (ndotl <= 0.0) continue;

        const float ndoth = saturate(h.z);
        const float vdoth = saturate(dot(v, h));

        // GGX 표본 pdf 가 D(H) 를 상쇄해 G * VdotH / (NdotH * NdotV) 만 남는다.
        const float g     = gv * GeometrySchlickGgxIbl(ndotl, roughness);
        const float gVis  = (g * vdoth) / max(ndoth * ndotv, 1e-6);

        const float x  = 1.0 - vdoth;
        const float x2 = x * x;
        const float fc = x2 * x2 * x;

        scale += (1.0 - fc) * gVis;
        bias  += fc * gVis;
    }

    return float2(scale, bias) / float(sampleCount);
}

// Frostbite 스페큘러 오클루전(Lagarde & de Rousiers 2014). 거칠기·스치는
// 각에 따라 AO 를 넓혀, 좁은 스페큘러 원뿔이 틈새로 새지 않게 한다.
float ComputeSpecularOcclusion(float ndotv, float ao, float roughness)
{
    return saturate(pow(ndotv + ao, exp2(-16.0 * roughness - 1.0)) - 1.0 + ao);
}

struct IblAmbient
{
    float3 diffuse;
    float3 specular;

    float3 Sum() { return diffuse + specular; }
};

// 에너지 보존 IBL 앰비언트: split-sum + Fdez-Aguera 2019 다중산란 보상.
//
// irradiance  : IblIrradiance 가 구운 값(= E/PI, 위 규약 참조)
// prefiltered : 반사 방향·거칠기 밉에서 읽은 프리필터 환경
// dfg         : BRDF LUT 의 (scale, bias)
// ao          : 재질 오클루전. 확산과 다중산란에 걸리고, 스페큘러에는
//               ComputeSpecularOcclusion 으로 넓혀 걸린다.
IblAmbient EvaluateIblAmbient(float3 albedo, float3 f0, float roughness,
    float metallic, float ndotv, float ao,
    float3 irradiance, float3 prefiltered, float2 dfg)
{
    // 거칠기 인식 프레넬을 스페큘러 가중치로 쓴다.
    const float3 ks     = FresnelSchlickRoughness(f0, ndotv, roughness);
    const float3 fssEss = ks * dfg.x + dfg.y;          // 단일산란 스페큘러

    // 다중산란 보상 ─ ems 는 단일산란 로브가 놓친 에너지, favg 는 코사인
    // 평균 프레넬이다. 닫힌 형태가 나머지 반사를 전부 더해 준다.
    const float  ems    = saturate(1.0 - (dfg.x + dfg.y));
    const float3 favg   = f0 + (1.0 - f0) / 21.0;
    const float3 fmsEms = (ems * fssEss * favg) / max(1.0 - favg * ems, 1e-4);

    // 금속은 확산이 없다. 단일·다중산란 스페큘러로 나가지 않은 몫만 확산으로
    // 간다 ─ 이렇게 묶어야 표면 전체가 에너지 보존을 지킨다.
    const float3 diffuseColor = albedo * (1.0 - metallic);
    const float3 kd           = diffuseColor * (1.0 - fssEss - fmsEms);

    const float specularAo = ComputeSpecularOcclusion(ndotv, ao, roughness);

    IblAmbient result;
    result.diffuse  = (fmsEms + kd) * irradiance * ao;
    result.specular = fssEss * prefiltered * specularAo;
    return result;
}
