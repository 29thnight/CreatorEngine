#ifndef DYNAMICCPP_EXPORTS
#include "EnhancedDeferredPass.h"
#include "EnhancedShadowPass.h"
#include "DX12DeviceResources.h"
#include "DX12PSOManager.h"
#include "DX12RootSignatureCache.h"

#include <d3dcompiler.h>
#include <sstream>

#pragma comment(lib, "d3dcompiler.lib")

namespace
{
    // 유니티 빌드에서 익명 네임스페이스가 파일 간 합쳐지므로 이름을 고유하게 둔다.
    constexpr const char* kDeferredShader = R"(
#define MAX_DEFERRED_LIGHTS 64

struct DeferredLight
{
    float4 position;      // w = 타입 (0 방향광 · 1 점광 · 2 스포트)
    float4 direction;     // w = 스포트 각도
    float4 color;         // rgb 색 · a 세기
    float4 attenuation;   // x 상수 · y 선형 · z 이차 · w 반경
};

#define SHADOW_CASCADE_COUNT 3

cbuffer LightingConstants : register(b0)
{
    float4x4      gInverseViewProjection;
    float4x4      gLightViewProjection[SHADOW_CASCADE_COUNT];
    float4        gEyePosition;
    float4        gCameraForward;
    float4        gCascadeSplits;   // xyz = 각 캐스케이드가 끝나는 뷰 깊이
    float4        gShadowBias;      // xyz = 캐스케이드별 깊이 편향 · w = 경사 비례 계수
    uint          gLightCount;
    uint          gHasShadow;
    float         gCascadeBlendBand; // 경계 블렌딩 폭(분할 깊이 비율) · 0이면 하드 스위치
    uint          gPadding;
    DeferredLight gLights[MAX_DEFERRED_LIGHTS];
};

Texture2D    gDiffuse    : register(t0);
Texture2D    gMetalRough : register(t1);
Texture2D    gNormal     : register(t2);
Texture2D    gEmissive   : register(t3);
Texture2D    gDepth      : register(t4);
Texture2DArray gShadowMap : register(t5);
SamplerState gSampler    : register(s0);

// 비교 샘플러. 하드웨어가 이웃 텍셀 넷을 비교하고 평균내 준다(2x2 PCF) —
// 직접 네 번 읽는 것보다 싸고, 가장자리가 부드러워진다.
SamplerComparisonState gShadowSampler : register(s1);

// 한 캐스케이드에서의 그림자 표본. slopeTan은 호출부가 한 번만 계산한다.
float SampleShadowCascade(uint cascade, float3 worldPosition, float slopeTan)
{
    const float4 lightSpace = mul(float4(worldPosition, 1.0f), gLightViewProjection[cascade]);
    const float3 projected = lightSpace.xyz / lightSpace.w;

    // 라이트 상자 밖은 그림자 정보가 없다. 가려진 것으로 치면 맵 경계에
    // 검은 띠가 생기므로 빛을 받는 것으로 둔다. 마지막 캐스케이드 너머
    // (그림자 거리 밖)도 이 경로로 걸러진다.
    if (any(abs(projected.xy) > 1.0f) || projected.z > 1.0f) return 1.0f;

    const float2 uv = float2(projected.x * 0.5f + 0.5f, 0.5f - projected.y * 0.5f);

    // 깊이 편향 = 캐스케이드 편향 x (1 + 경사 계수 x tan). 상수 항은 먼
    // 캐스케이드일수록 크고(텍셀이 넓다), 경사 항은 표면이 빛과 비스듬할수록
    // 크다 — 텍셀 하나 안에서 실제 깊이가 tan만큼 변하기 때문이다.
    const float bias = gShadowBias[cascade] * (1.0f + gShadowBias.w * slopeTan);

    return gShadowMap.SampleCmpLevelZero(gShadowSampler,
        float3(uv, (float)cascade), projected.z - bias);
}

// 방향광 캐스케이드 그림자. 반환값 1은 빛을 받음, 0은 가려짐.
// normal·toLight는 경사 비례 편향의 재료다 — 호출부(광원 루프)가 이미 갖고 있다.
float SampleShadow(float3 worldPosition, float3 normal, float3 toLight)
{
    if (gHasShadow == 0) return 1.0f;

    // 어느 캐스케이드인지는 카메라로부터의 뷰 깊이로 고른다. 캐스케이드를
    // 하나씩 투영해 보고 들어맞는 것을 찾는 방법도 있지만 그쪽은 행렬 곱이
    // 최대 셋이다 — 분할 지점이 뷰 깊이 기준이라 비교 둘이면 같은 답이 나온다.
    const float viewDepth = dot(worldPosition - gEyePosition.xyz, gCameraForward.xyz);

    uint cascade = 0;
    if (viewDepth > gCascadeSplits.x) cascade = 1;
    if (viewDepth > gCascadeSplits.y) cascade = 2;

    // tan(경사각) = sqrt(1-cos²)/cos. 수평에 가까울수록 폭주하므로 8로 자른다 —
    // 그 너머는 어차피 ndotl이 0에 가까워 빛 기여 자체가 사라진다.
    const float ndotl = saturate(dot(normal, toLight));
    const float slopeTan = min(sqrt(max(1.0f - ndotl * ndotl, 0.0f)) / max(ndotl, 1e-3f), 8.0f);

    float shadow = SampleShadowCascade(cascade, worldPosition, slopeTan);

    // 경계 블렌딩 — 분할 앞 구간에서 다음 캐스케이드와 섞는다. 두 캐스케이드는
    // 해상도가 달라 그림자 가장자리가 미세하게 어긋나는데, 하드 스위치면 그
    // 어긋남이 경계에서 선으로 보인다. 비용은 구간 안에서만 표본 하나 추가다.
    if (gCascadeBlendBand > 0.0f && cascade < SHADOW_CASCADE_COUNT - 1)
    {
        const float split = (cascade == 0) ? gCascadeSplits.x : gCascadeSplits.y;
        const float bandStart = split * (1.0f - gCascadeBlendBand);
        if (viewDepth > bandStart)
        {
            const float blend = saturate((viewDepth - bandStart) / max(split - bandStart, 1e-4f));
            shadow = lerp(shadow,
                SampleShadowCascade(cascade + 1, worldPosition, slopeTan), blend);
        }
    }

    return shadow;
}

struct VSOut
{
    float4 position : SV_POSITION;
    float2 uv       : TEXCOORD0;
};

VSOut VSMain(uint id : SV_VertexID)
{
    // 정점 버퍼 없는 풀스크린 삼각형. 쿼드보다 낫다 — 대각선에서 픽셀이
    // 두 번 셰이딩되는 일이 없고, 정점도 셋뿐이다.
    VSOut output;
    output.uv = float2((id << 1) & 2, id & 2);
    output.position = float4(output.uv * float2(2, -2) + float2(-1, 1), 0, 1);
    return output;
}

// ── GGX 정반사 ──
//
// 거칠기·금속성을 확산 감쇠로만 쓰던 것을 실제 BRDF로 바꾼다. 그 전에는
// 재질 격자를 그려도 가로(거칠기)·세로(금속성)가 거의 같아 보였다 —
// 값이 셰이더에 닿는지는 확인할 수 있어도 '무엇을 하는지'는 볼 수 없었다.
//
// Trowbridge-Reitz(GGX) 분포 + Smith 높이 상관 가시성 + Schlick 프레넬.
// glTF/UE4가 쓰는 조합이라 저작 도구에서 만든 값이 의도대로 보인다.
float DistributionGGX(float ndoth, float alpha)
{
    const float a2 = alpha * alpha;
    const float d = ndoth * ndoth * (a2 - 1.0f) + 1.0f;
    return a2 / max(3.14159265f * d * d, 1e-6f);
}

float VisibilitySmith(float ndotv, float ndotl, float alpha)
{
    // 높이 상관(height-correlated) Smith. 분모의 4*NdotV*NdotL까지 흡수한 형태라
    // 호출부에서 다시 나누지 않는다.
    const float a2 = alpha * alpha;
    const float v = ndotl * sqrt(ndotv * ndotv * (1.0f - a2) + a2);
    const float l = ndotv * sqrt(ndotl * ndotl * (1.0f - a2) + a2);
    return 0.5f / max(v + l, 1e-6f);
}

float3 FresnelSchlick(float3 f0, float vdoth)
{
    return f0 + (1.0f - f0) * pow(saturate(1.0f - vdoth), 5.0f);
}

float4 PSMain(VSOut input) : SV_TARGET
{
    const float3 albedo   = gDiffuse.Sample(gSampler, input.uv).rgb;
    const float3 normal   = normalize(gNormal.Sample(gSampler, input.uv).rgb * 2.0f - 1.0f);
    const float3 emissive = gEmissive.Sample(gSampler, input.uv).rgb;
    const float2 orm      = gMetalRough.Sample(gSampler, input.uv).gb;
    const float  rough    = saturate(orm.x);
    const float  metallic = saturate(orm.y);
    const float  depth    = gDepth.Sample(gSampler, input.uv).r;

    // 깊이에서 월드 위치를 복원한다. GBuffer에 위치를 따로 저장하는 방법도 있지만
    // 타깃 하나가 더 늘고 대역폭이 그만큼 든다 — 깊이가 이미 있으니 역행렬로 푼다.
    const float2 ndc = float2(input.uv.x * 2.0f - 1.0f, 1.0f - input.uv.y * 2.0f);
    const float4 clipPosition = float4(ndc, depth, 1.0f);
    const float4 worldHomogeneous = mul(clipPosition, gInverseViewProjection);
    const float3 worldPosition = worldHomogeneous.xyz / worldHomogeneous.w;

    // 시선 방향과 재질 항은 광원마다 같으므로 루프 밖에서 한 번만 만든다.
    const float3 viewDirection = normalize(gEyePosition.xyz - worldPosition);
    const float  ndotv = saturate(dot(normal, viewDirection)) + 1e-5f;

    // 유전체의 기본 반사율 0.04는 관행값이다(대부분의 비금속이 4% 근처).
    // 금속은 알베도가 곧 반사색이다.
    const float3 f0 = lerp(0.04f, albedo, metallic);
    const float3 diffuseAlbedo = albedo / 3.14159265f;

    // 거칠기를 그대로 쓰면 0에서 정반사가 점 하나가 되어 화면에서 사라진다.
    // 제곱해 쓰는 것이 관행이고, 하한을 두어 완전 거울을 피한다.
    const float alpha = max(rough * rough, 1e-3f);

    float3 lit = 0.0f;
    for (uint i = 0; i < gLightCount; ++i)
    {
        const DeferredLight light = gLights[i];
        const uint type = (uint)light.position.w;

        float3 toLight;
        float  attenuation = 1.0f;

        if (type == 0)
        {
            // 방향광 — 위치와 감쇠가 없다. 그림자는 이쪽에만 붙는다.
            toLight = -normalize(light.direction.xyz);
            attenuation = SampleShadow(worldPosition, normal, toLight);
        }
        else
        {
            const float3 offset = light.position.xyz - worldPosition;
            const float  distance = length(offset);
            toLight = offset / max(distance, 1e-4f);

            attenuation = 1.0f / (light.attenuation.x
                + light.attenuation.y * distance
                + light.attenuation.z * distance * distance);

            // 반경 밖은 잘라낸다. 감쇠식만 쓰면 먼 광원이 미세하게 계속 기여해
            // 광원 수에 비례해 화면이 밝아진다.
            attenuation *= saturate(1.0f - distance / max(light.attenuation.w, 1e-4f));

            if (type == 2)
            {
                // 스포트 — 원뿔 밖을 부드럽게 끈다.
                const float cosAngle = dot(-toLight, normalize(light.direction.xyz));
                const float cutoff = cos(light.direction.w * 0.5f);
                attenuation *= smoothstep(cutoff, lerp(cutoff, 1.0f, 0.2f), cosAngle);
            }
        }

        const float ndotl = saturate(dot(normal, toLight));
        if (ndotl <= 0.0f || attenuation <= 0.0f) continue;

        const float3 halfVector = normalize(toLight + viewDirection);
        const float  ndoth = saturate(dot(normal, halfVector));
        const float  vdoth = saturate(dot(viewDirection, halfVector));

        const float3 fresnel = FresnelSchlick(f0, vdoth);
        const float3 specular = fresnel
            * DistributionGGX(ndoth, alpha)
            * VisibilitySmith(ndotv, ndotl, alpha);

        // 에너지 보존: 반사되지 않은 몫만 확산으로 간다. 금속은 확산이 없다.
        const float3 kd = (1.0f - fresnel) * (1.0f - metallic);

        const float3 radiance = light.color.rgb * light.color.a * ndotl * attenuation;
        lit += (kd * diffuseAlbedo + specular) * radiance;
    }

    return float4(lit + emissive, 1.0f);
}
)";

    bool CompileDeferredShader(const char* entry, const char* target,
        Microsoft::WRL::ComPtr<ID3DBlob>& outBlob, std::string& outError)
    {
        Microsoft::WRL::ComPtr<ID3DBlob> errors;
        const HRESULT hr = D3DCompile(kDeferredShader, strlen(kDeferredShader), nullptr,
            nullptr, nullptr, entry, target, 0, 0, &outBlob, &errors);
        if (FAILED(hr))
        {
            outError = std::string("Deferred 셰이더 컴파일 실패(") + entry + "): ";
            if (errors) outError += static_cast<const char*>(errors->GetBufferPointer());
            return false;
        }
        return true;
    }
}

bool EnhancedDeferredPass::Initialize(const EnhancedFrameContext& context, std::string& outError)
{
    if (nullptr == context.resources || nullptr == context.psoManager ||
        nullptr == context.rootSignatures)
    {
        outError = "Deferred 패스 컨텍스트가 불완전하다";
        return false;
    }

    auto* device = context.resources->GetDevice();

    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc{};
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.NumDescriptors = 1;
    if (FAILED(device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_rtvHeap))))
    {
        outError = "Deferred RTV 힙 생성 실패";
        return false;
    }

    ComPtr<ID3DBlob> vsBlob;
    ComPtr<ID3DBlob> psBlob;
    if (!CompileDeferredShader("VSMain", "vs_5_0", vsBlob, outError)) return false;
    if (!CompileDeferredShader("PSMain", "ps_5_0", psBlob, outError)) return false;

    // SRV를 한 테이블로 묶는다. 디스크립터 링에서 연속으로 잘라 쓰므로
    // 테이블 하나면 충분하고, 바인딩 호출도 한 번이다(상태 변경 최소화).
    D3D12_DESCRIPTOR_RANGE srvRange{};
    srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange.NumDescriptors = 6;   // diffuse · metalRough · normal · emissive · depth · shadow

    D3D12_DESCRIPTOR_RANGE samplerRange{};
    samplerRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
    samplerRange.NumDescriptors = 2;   // 일반 · 비교(그림자)

    D3D12_ROOT_PARAMETER params[3]{};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[0].DescriptorTable.NumDescriptorRanges = 1;
    params[0].DescriptorTable.pDescriptorRanges = &srvRange;
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[1].DescriptorTable.NumDescriptorRanges = 1;
    params[1].DescriptorTable.pDescriptorRanges = &samplerRange;
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    // 광원 상수. 루트 CBV로 넘긴다 — 업로드 링 주소를 그대로 꽂으면 되고,
    // 프레임마다 한 번이라 디스크립터를 만들 이유가 없다.
    params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[2].Descriptor.ShaderRegister = 0;
    params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rootDesc{};
    rootDesc.NumParameters = _countof(params);
    rootDesc.pParameters = params;

    const auto root = context.rootSignatures->GetOrCreate(rootDesc, outError);
    if (!root.IsValid()) return false;
    m_rootSignature = root.signature;

    DX12GraphicsPipelineDesc desc{};
    desc.vsBytecode = vsBlob->GetBufferPointer();
    desc.vsSize = vsBlob->GetBufferSize();
    desc.psBytecode = psBlob->GetBufferPointer();
    desc.psSize = psBlob->GetBufferSize();
    desc.rootSignature = root.signature;
    desc.rootSignatureId = root.id;
    desc.numRenderTargets = 1;
    desc.rtvFormats[0] = kOutputFormat;

    m_pso = context.psoManager->GetOrCreate(desc, outError);
    if (nullptr == m_pso) return false;

    // 샘플러 둘을 연속으로 만든다 — 테이블은 연속이어야 하므로 따로 만들어
    // 인접을 기대하면 안 된다.
    D3D12_SAMPLER_DESC samplers[2]{};

    samplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
    samplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplers[0].MaxLOD = D3D12_FLOAT32_MAX;

    // 그림자 비교 샘플러. 경계 색을 1로 두어 맵 밖이 '빛을 받음'이 되게 한다.
    samplers[1].Filter = D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
    samplers[1].AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    samplers[1].AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    samplers[1].AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    samplers[1].ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    samplers[1].BorderColor[0] = 1.f;
    samplers[1].BorderColor[1] = 1.f;
    samplers[1].BorderColor[2] = 1.f;
    samplers[1].BorderColor[3] = 1.f;
    samplers[1].MaxLOD = D3D12_FLOAT32_MAX;

    m_sampler = context.resources->GetSamplerHeap().CreateRange(samplers, 2);
    if (0 == m_sampler.ptr)
    {
        outError = "Deferred 샘플러 생성 실패";
        return false;
    }

    return true;
}

bool EnhancedDeferredPass::PrepareFrame(const EnhancedFrameContext& context, std::string& outError)
{
    m_frameLights.clear();
    m_droppedLights = 0;

    if (nullptr != context.camera)
    {
        const Mathf::xMatrix viewProjection =
            XMMatrixMultiply(context.camera->view, context.camera->projection);

        // HLSL이 행 우선으로 읽으므로 전치해서 넣는다. 역행렬을 여기서 구해 두면
        // 픽셀마다 다시 구하지 않는다.
        m_inverseViewProjection = XMMatrixTranspose(XMMatrixInverse(nullptr, viewProjection));
        m_eyePosition = context.camera->eyePosition;
    }
    else
    {
        m_inverseViewProjection = XMMatrixIdentity();
        m_eyePosition = Mathf::Vector4{};
    }

    if (nullptr == context.lights) return true;

    for (const auto& light : *context.lights)
    {
        if (m_frameLights.size() >= kMaxLights)
        {
            // 조용히 버리지 않는다. 광원이 몇 개부터 사라지는지 모르면
            // '어느 순간부터 어두워진다'로만 드러난다.
            ++m_droppedLights;
            continue;
        }
        m_frameLights.push_back(light);
    }

    if (0 != m_droppedLights)
    {
        outError = "광원 " + std::to_string(m_droppedLights) + "개가 한도("
            + std::to_string(kMaxLights) + ")를 넘어 잘렸다";
    }

    return true;
}

void EnhancedDeferredPass::Declare(EnhancedRenderGraph& graph, const EnhancedFrameContext& context)
{
    RGTextureDesc outputDesc{};
    outputDesc.width = context.width;
    outputDesc.height = context.height;
    outputDesc.format = kOutputFormat;
    outputDesc.allowRenderTarget = true;
    outputDesc.name = "Deferred.Lighting";
    m_output = graph.CreateTexture(outputDesc);

    // GBuffer 넷을 읽고 하나에 쓴다. 이 선언만으로 그래프가
    // RENDER_TARGET → PIXEL_SHADER_RESOURCE 전이를 만들어 준다.
    const std::vector<EnhancedRenderGraph::RGPassUsage> usages = {
        { m_inputs.diffuse,    RGResourceState::ShaderResource },
        { m_inputs.metalRough, RGResourceState::ShaderResource },
        { m_inputs.normal,     RGResourceState::ShaderResource },
        { m_inputs.emissive,   RGResourceState::ShaderResource },
        // 깊이는 DEPTH_WRITE에서 읽기 상태로 넘어와야 한다. 그래프가 알아서
        // 전이를 만들지만, 여기서 선언하지 않으면 만들지 않는다.
        { m_inputs.depth,      RGResourceState::ShaderResource },
        { m_shadowMap,         RGResourceState::ShaderResource },   // 캐스케이드 배열
        { m_output,            RGResourceState::RenderTarget },
    };

    graph.AddPass(GetName(), usages,
        [this, &context](const EnhancedRenderGraph::ExecuteContext& executeContext)
        {
            auto* commandList = executeContext.commandList;
            auto* device = context.resources->GetDevice();

            const auto rtv = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
            device->CreateRenderTargetView(executeContext.Resolve(m_output), nullptr, rtv);

            // SRV 여섯을 디스크립터 링에서 연속으로 자른다 — 테이블은 연속이어야 한다.
            const auto srvRange = context.resources->GetDescriptorRing().Allocate(6);
            if (!srvRange.IsValid()) return;

            const RGHandle sources[4] = {
                m_inputs.diffuse, m_inputs.metalRough, m_inputs.normal, m_inputs.emissive };
            for (uint32_t i = 0; i < 4; ++i)
            {
                device->CreateShaderResourceView(executeContext.Resolve(sources[i]), nullptr,
                    srvRange.CpuAt(i));
            }

            // 깊이는 포맷을 바꿔서 봐야 한다. D32_FLOAT로 만든 리소스를 SRV로
            // 읽으려면 명시적으로 알려 줘야 하고, nullptr 설명으로는 실패한다.
            D3D12_SHADER_RESOURCE_VIEW_DESC depthSrv{};
            depthSrv.Format = DXGI_FORMAT_R32_FLOAT;
            depthSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            depthSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            depthSrv.Texture2D.MipLevels = 1;
            device->CreateShaderResourceView(executeContext.Resolve(m_inputs.depth),
                &depthSrv, srvRange.CpuAt(4));

            // 그림자 맵도 깊이지만 배열이라 차원까지 바꿔 봐야 한다.
            D3D12_SHADER_RESOURCE_VIEW_DESC shadowSrv{};
            shadowSrv.Format = DXGI_FORMAT_R32_FLOAT;
            shadowSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
            shadowSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            shadowSrv.Texture2DArray.MipLevels = 1;
            shadowSrv.Texture2DArray.ArraySize = kShadowCascadeCount;
            device->CreateShaderResourceView(executeContext.Resolve(m_shadowMap),
                &shadowSrv, srvRange.CpuAt(5));

            // 광원 상수를 업로드 링에 올린다.
            LightingConstants constants{};
            constants.inverseViewProjection = m_inverseViewProjection;
            for (uint32_t i = 0; i < kShadowCascadeCount; ++i)
            {
                constants.lightViewProjection[i] =
                    XMMatrixTranspose(m_shadowData.lightViewProjection[i]);
            }
            constants.eyePosition = m_eyePosition;
            constants.cameraForward = m_shadowData.cameraForward;
            constants.cascadeSplits = m_shadowData.splitDepths;
            constants.shadowBias = m_shadowData.bias;
            constants.lightCount = static_cast<uint32_t>(m_frameLights.size());
            constants.hasShadow = m_shadowData.enabled ? 1u : 0u;
            constants.cascadeBlendBand = m_shadowData.cascadeBlendBand;
            for (size_t i = 0; i < m_frameLights.size(); ++i)
            {
                constants.lights[i] = m_frameLights[i];
            }

            const auto lightConstants = context.resources->GetUploadRing().Allocate(
                sizeof(LightingConstants), DX12UploadRing::kConstantBufferAlignment);
            if (!lightConstants.IsValid()) return;
            memcpy(lightConstants.cpuAddress, &constants, sizeof(constants));

            const D3D12_VIEWPORT viewport{ 0.f, 0.f,
                static_cast<float>(context.width), static_cast<float>(context.height), 0.f, 1.f };
            const D3D12_RECT scissor{ 0, 0,
                static_cast<LONG>(context.width), static_cast<LONG>(context.height) };
            commandList->RSSetViewports(1, &viewport);
            commandList->RSSetScissorRects(1, &scissor);
            commandList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);

            ID3D12DescriptorHeap* heaps[] = {
                context.resources->GetDescriptorRing().GetHeap(),
                context.resources->GetSamplerHeap().GetHeap() };
            commandList->SetDescriptorHeaps(2, heaps);

            commandList->SetGraphicsRootSignature(m_rootSignature);
            commandList->SetPipelineState(m_pso);
            commandList->SetGraphicsRootDescriptorTable(0, srvRange.gpu);
            commandList->SetGraphicsRootDescriptorTable(1, m_sampler);
            commandList->SetGraphicsRootConstantBufferView(2, lightConstants.gpuAddress);

            commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            commandList->DrawInstanced(3, 1, 0, 0);
        },
        // 이 패스의 결과가 최종 출력이다. GBuffer는 이 패스가 읽으므로
        // 뿌리 표시 없이도 컬링에서 살아남는다 — 그것이 3-5 컬링의 실전 확인이다.
        true);
}

void EnhancedDeferredPass::Shutdown()
{
    m_rtvHeap.Reset();
    m_pso = nullptr;
    m_rootSignature = nullptr;
}

#endif
