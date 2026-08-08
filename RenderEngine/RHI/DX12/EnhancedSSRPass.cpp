#ifndef DYNAMICCPP_EXPORTS
#include "EnhancedSSRPass.h"
#include "DX12DeviceResources.h"
#include "DX12PSOManager.h"
#include "DX12RootSignatureCache.h"
#include "EnhancedRenderGraph.h"
#include "RHIEncoder.h"

#include <d3dcompiler.h>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

#pragma comment(lib, "d3dcompiler.lib")

namespace
{
    // 유니티 빌드에서 익명 네임스페이스가 합쳐지므로 이름을 고유하게 둔다.
    std::string SsrHrToString(HRESULT hr)
    {
        std::ostringstream oss;
        oss << "HRESULT 0x" << std::hex << static_cast<unsigned long>(hr);
        return oss.str();
    }

    // DX11 SSR.ps.hlsl의 이식.
    //
    // 광선 행진·잡음·두께 판정·가중치·최종 혼합을 그대로 옮겼다. 상수도
    // 그대로다. 바꾼 것은 넷이다:
    //
    //   · 정점을 SV_VertexID 풀스크린 삼각형으로. 원본은 Fullscreen.vs의
    //     4정점 트라이앵글 스트립인데 UV 대응이 같다(왼쪽 위 (0,0)).
    //   · prevSSR(t4) 선언과 두 번째 렌더 타깃을 뺀다 — 원본이 읽지 않는다.
    //     그 자리를 비트플래그가 메워 t4로 한 칸 당겨졌다.
    //   · 행렬 규약을 DX12 쪽에 맞춘다(CPU가 전치, mul(v, M)).
    //   · 주석 처리된 죽은 줄들을 옮기지 않는다.
    //
    // ★ 옮기지 '않은' 것은 없다. 아래 셋은 원본의 결함이지만 그대로 둔다:
    //   depth>=1 분기에 return이 없는 것, reflectFactor·edgeFade를 구해
    //   놓고 안 쓰는 것, screenSize가 (0,0)이라 비트플래그가 텍셀 (0,0)만
    //   보는 것. 고치면 그림이 바뀌고, 기준선은 DX11이다.
    constexpr const char* kSSRShader = R"(
Texture2D       gDepth      : register(t0);
Texture2D       gColor      : register(t1);
Texture2D       gMetalRough : register(t2);
Texture2D       gNormal     : register(t3);
Texture2D<uint> gBitmask    : register(t4);

SamplerState gLinearSampler : register(s0);
SamplerState gPointSampler  : register(s1);

cbuffer SSRConstants : register(b0)
{
    float4x4 gInverseProjection;
    float4x4 gInverseView;
    float4x4 gViewProjection;
    float4   gCameraPosition;
    float    gStepSize;
    float    gMaxThickness;
    float    gTime;
    int      gMaxRayCount;
    float2   gScreenSize;
    float2   gPadding;
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

float3 ReconstructWorldPosFromDepth(float2 uv, float depth)
{
    // 클립 공간 [-1,1]. y를 뒤집어 위가 +y가 되게 한다.
    float2 clipXY = uv * 2.0f - 1.0f;
    clipXY.y = -clipXY.y;

    // 깊이는 선형화되지 않았고 DirectX 규약대로 [0,1]이다.
    float4 clipSpace = float4(clipXY, depth, 1.0f);
    float4 viewSpace = mul(clipSpace, gInverseProjection);

    viewSpace /= viewSpace.w;

    float4 worldSpace = mul(viewSpace, gInverseView);
    return worldSpace.xyz;
}

float SsrNoise(float2 seed)
{
    return frac(sin(dot(seed.xy, float2(12.9898f, 78.233f))) * 43758.5453f);
}

float4 Raytrace(float3 reflectionWorld, const int maxCount, float stepSize, float3 pos, float2 uv)
{
    float4 color = float4(0.0f, 0.0f, 0.0f, 0.0f);
    float3 step = stepSize * reflectionWorld;

    [loop]
    for (int i = 1; i <= maxCount; i++)
    {
        float3 ray = (i + SsrNoise(uv + gTime)) * step;
        float3 rayPos = pos + ray;
        float4 vpPos = mul(float4(rayPos, 1.0f), gViewProjection);

        float  rayDepth = vpPos.z / vpPos.w;
        float2 rayUv = vpPos.xy / vpPos.w * 0.5f + 0.5f;
        rayUv.y = 1.0f - rayUv.y;   // y 뒤집기(DirectX 좌표계)

        float gbufferDepth = gDepth.Sample(gLinearSampler, rayUv).r;
        if (rayUv.x < 0.0f || rayUv.x > 1.0f || rayUv.y < 0.0f || rayUv.y > 1.0f)
            continue;

        // 광선이 표면 바로 뒤로 들어갔는가. 두께를 넘으면 그 표면 뒤의
        // 빈 공간을 지나간 것이라 반사로 치지 않는다.
        if (rayDepth - gbufferDepth > 0 && rayDepth - gbufferDepth < gMaxThickness)
        {
            float a = 0.3f * pow(min(1.0f, (stepSize * maxCount / 2) / length(ray)), 2.0f);
            color = color * (1.0f - a) + float4(gColor.Sample(gLinearSampler, rayUv).rgb, 1.0f) * a;
            break;
        }
    }

    return color;
}

float4 PSMain(VSOut input) : SV_TARGET
{
    float4 color = gColor.Sample(gLinearSampler, input.texCoord);

    // ★ 원본 그대로: gScreenSize가 (0,0)이라 모든 픽셀이 텍셀 (0,0)을 짚는다.
    if (gBitmask.Load(int3(int2(input.texCoord * gScreenSize), 0)) & 1 << 9)
    {
        return color;
    }

    float depth = gDepth.Sample(gLinearSampler, input.texCoord).r;

    // ★ 원본 그대로: return이 없다. 아래로 계속 흘러 마지막 줄이 덮어쓴다.
    float4 output = color;
    if (depth >= 1.0f)
        output = color;

    float3 normal = gNormal.Sample(gLinearSampler, input.texCoord).xyz;
    normal = normalize(normal * 2.0f - 1.0f);

    float2 metalRough = gMetalRough.Sample(gLinearSampler, input.texCoord).rg;

    float4 worldSpacePosition = float4(
        ReconstructWorldPosFromDepth(input.texCoord, depth), 1.0f);
    float3 camDir = normalize(worldSpacePosition.xyz - gCameraPosition.xyz);
    float3 refDir = (normalize(reflect(camDir, normal))).rgb;

    // ★ 원본 그대로: reflectFactor와 edgeFade를 구하지만 최종 줄이 쓰지 않는다.
    float reflectFactor = (1.0f - metalRough.y) * (0.04f * (1.0f - metalRough.x) + metalRough.x);

    float4 reflectedColor = Raytrace(refDir, gMaxRayCount, gStepSize,
        worldSpacePosition.rgb, input.texCoord);

    float edgeFade = saturate(1.0f - pow(length(input.texCoord.xy - 0.5f) * 2.0f, 2.0f));
    reflectFactor *= edgeFade;

    output = lerp(color, color + reflectedColor, metalRough.x);
    return output;
}
)";

    struct SSRConstants
    {
        Mathf::Matrix  inverseProjection{};
        Mathf::Matrix  inverseView{};
        Mathf::Matrix  viewProjection{};
        Mathf::Vector4 cameraPosition{};
        float          stepSize{ 0.f };
        float          maxThickness{ 0.f };
        float          time{ 0.f };
        int32_t        maxRayCount{ 0 };

        // ★ DX11이 채우지 않는 자리다. Mathf::Vector2가 SimpleMath라 기본
        // 생성자가 (0,0)을 넣고, 그 값이 그대로 셰이더로 간다. 여기서도
        // 0으로 둬야 같은 그림이 나온다 — 화면 크기를 넣으면 비트플래그
        // 게이트가 원본과 다르게 동작한다(그쪽이 옳은 동작이지만, 그것은
        // DX11을 고치는 일이지 이식하는 일이 아니다).
        float          screenSize[2]{ 0.f, 0.f };
        float          padding[2]{};
    };

    bool CompileSSRShader(const char* entry, const char* target,
        Microsoft::WRL::ComPtr<ID3DBlob>& outBlob, std::string& outError)
    {
        Microsoft::WRL::ComPtr<ID3DBlob> errors;
        const HRESULT hr = D3DCompile(kSSRShader, strlen(kSSRShader), nullptr, nullptr,
            nullptr, entry, target, 0, 0, &outBlob, &errors);
        if (FAILED(hr))
        {
            outError = std::string("SSR 셰이더 컴파일 실패(") + entry + "): ";
            if (errors) outError += static_cast<const char*>(errors->GetBufferPointer());
            else        outError += SsrHrToString(hr);
            return false;
        }
        return true;
    }
}

bool EnhancedSSRPass::Initialize(const EnhancedFrameContext& context, std::string& outError)
{
    if (nullptr == context.resources || nullptr == context.psoManager ||
        nullptr == context.rootSignatures)
    {
        outError = "SSR 패스 컨텍스트가 불완전하다";
        return false;
    }

    return CreatePipelines(context, outError);
}

bool EnhancedSSRPass::CreatePipelines(const EnhancedFrameContext& context, std::string& outError)
{
    // b0 상수 · t0~t4 테이블(깊이·색·금속거칠기·노멀·비트마스크) ·
    // s0 선형 클램프 · s1 포인트 클램프.
    D3D12_DESCRIPTOR_RANGE srvRange{};
    srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange.NumDescriptors = 5;
    srvRange.BaseShaderRegister = 0;

    D3D12_ROOT_PARAMETER params[2]{};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[0].Descriptor.ShaderRegister = 0;
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[1].DescriptorTable.NumDescriptorRanges = 1;
    params[1].DescriptorTable.pDescriptorRanges = &srvRange;
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_STATIC_SAMPLER_DESC samplers[2]{};
    samplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    samplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplers[0].MaxLOD = D3D12_FLOAT32_MAX;
    samplers[0].ShaderRegister = 0;
    samplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    samplers[1] = samplers[0];
    samplers[1].Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
    samplers[1].ShaderRegister = 1;

    D3D12_ROOT_SIGNATURE_DESC rootDesc{};
    rootDesc.NumParameters = _countof(params);
    rootDesc.pParameters = params;
    rootDesc.NumStaticSamplers = _countof(samplers);
    rootDesc.pStaticSamplers = samplers;

    const auto root = context.rootSignatures->GetOrCreate(rootDesc, outError);
    if (!root.IsValid()) return false;
    m_rootSignature = root.signature;

    ComPtr<ID3DBlob> vsBlob;
    ComPtr<ID3DBlob> psBlob;
    if (!CompileSSRShader("VSMain", "vs_5_0", vsBlob, outError)) return false;
    if (!CompileSSRShader("PSMain", "ps_5_0", psBlob, outError)) return false;

    DX12GraphicsPipelineDesc desc{};
    desc.vsBytecode = vsBlob->GetBufferPointer();
    desc.vsSize = vsBlob->GetBufferSize();
    desc.psBytecode = psBlob->GetBufferPointer();
    desc.psSize = psBlob->GetBufferSize();
    desc.rootSignature = root.signature;
    desc.rootSignatureId = root.id;
    desc.inputElements = nullptr;
    desc.inputElementCount = 0;
    desc.topologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

    // 숨어 있던 암묵 상태를 명시한다(SSS·Decal과 같은 부류). 결과는
    // 덮어쓰는 것이 맞고, 풀스크린이라 깊이도 보지 않는다.
    desc.depthEnable = false;
    desc.blendEnable = false;
    desc.cullMode = D3D12_CULL_MODE_NONE;
    desc.numRenderTargets = 1;
    desc.rtvFormats[0] = kOutputFormat;

    m_pso = context.psoManager->GetOrCreate(desc, outError);
    if (nullptr == m_pso) return false;

    return true;
}

bool EnhancedSSRPass::PrepareFrame(const EnhancedFrameContext& context, std::string& outError)
{
    (void)outError;

    m_width = context.width;
    m_height = context.height;

    if (nullptr != context.camera)
    {
        m_inverseProjection = XMMatrixTranspose(context.camera->inverseProjection);
        m_inverseView = XMMatrixTranspose(context.camera->inverseView);
        m_viewProjection = XMMatrixTranspose(
            context.camera->view * context.camera->projection);
        m_cameraPosition = context.camera->eyePosition;
    }

    return true;
}

void EnhancedSSRPass::Declare(EnhancedRenderGraph& graph, const EnhancedFrameContext& context)
{
    // 꺼져 있으면 입력을 그대로 흘린다. 뒤 패스가 '켜졌나'를 따지지 않고
    // GetOutput()만 이으면 되도록 — DX11은 여기서 return해 씬 컬러가
    // 손대지 않은 채 남는데, 핸들을 그대로 넘기는 것이 같은 뜻이다.
    if (!m_enabled)
    {
        m_output = m_inputs.color;
        return;
    }

    m_output = RGHandle{};

    if (nullptr == m_pso || 0 == m_width || 0 == m_height) return;
    if (!m_inputs.color.IsValid() || !m_inputs.depth.IsValid() ||
        !m_inputs.metalRough.IsValid() || !m_inputs.normal.IsValid() ||
        !m_inputs.bitmask.IsValid())
    {
        // 입력이 모자라면 켜져 있어도 그릴 수 없다. 입력을 흘려보내
        // 체인이 끊기지 않게 한다.
        m_output = m_inputs.color;
        return;
    }

    // ★ 씬 컬러 복사가 사라진 자리.
    //
    // DX11은 제자리에 쓰느라 씬 컬러를 통째로 복사했다. 여기서는 입력을
    // 읽어 새 transient에 쓴다 — 출력이 입력 이미지만으로 정해지므로
    // 같은 픽셀이 나오고, 읽는 것과 쓰는 것이 갈려 복사가 필요 없다.
    RGTextureDesc desc{};
    desc.width = m_width;
    desc.height = m_height;
    desc.format = kOutputFormat;
    desc.allowRenderTarget = true;
    desc.name = "SSR.Output";
    m_output = graph.CreateTexture(desc);

    const std::vector<EnhancedRenderGraph::RGPassUsage> usages = {
        { m_inputs.color,      RGResourceState::ShaderResource },
        { m_inputs.depth,      RGResourceState::ShaderResource },
        { m_inputs.metalRough, RGResourceState::ShaderResource },
        { m_inputs.normal,     RGResourceState::ShaderResource },
        { m_inputs.bitmask,    RGResourceState::ShaderResource },
        { m_output,            RGResourceState::RenderTarget },
    };

    graph.AddPass(GetName(), usages,
        [this, &context](const EnhancedRenderGraph::ExecuteContext& executeContext)
        {
            RHIEncoder& encoder = *executeContext.encoder;
            auto* commandList = executeContext.commandList;

            ID3D12Resource* const colors[] = { executeContext.Resolve(m_output) };
            const auto targets = context.resources->CreateRenderTargets(colors);
            if (!targets.IsValid()) return;

            encoder.SetViewportAndScissor(m_width, m_height);
            encoder.BindRenderTargets(targets);

            // 테이블 하나로 잘라 받는다(R2). 깊이만 포맷을 명시한다 —
            // D32_FLOAT 리소스를 SRV로 읽으려면 R32_FLOAT로 봐야 한다.
            const RHIBindingDesc bindings[] = {
                RHIBindingDesc::Srv2D(executeContext.Resolve(m_inputs.depth),
                    DXGI_FORMAT_R32_FLOAT),
                RHIBindingDesc::Srv(executeContext.Resolve(m_inputs.color)),
                RHIBindingDesc::Srv(executeContext.Resolve(m_inputs.metalRough)),
                RHIBindingDesc::Srv(executeContext.Resolve(m_inputs.normal)),
                RHIBindingDesc::Srv(executeContext.Resolve(m_inputs.bitmask)),
            };
            const RHIBindingTable srvTable = context.resources->CreateBindings(bindings);
            if (!srvTable.IsValid()) return;

            SSRConstants constants{};
            constants.inverseProjection = m_inverseProjection;
            constants.inverseView = m_inverseView;
            constants.viewProjection = m_viewProjection;
            constants.cameraPosition = m_cameraPosition;
            constants.stepSize = m_tuning.stepSize;
            constants.maxThickness = m_tuning.maxThickness;
            constants.time = m_time;
            constants.maxRayCount = m_tuning.maxRayCount;
            // screenSize는 채우지 않는다 — 위 구조체 주석 참고.

            const auto cb = context.resources->GetUploadRing().Allocate(
                sizeof(SSRConstants), DX12UploadRing::kConstantBufferAlignment);
            if (!cb.IsValid()) return;
            memcpy(cb.cpuAddress, &constants, sizeof(constants));

            encoder.SetPipeline(RHIBindPoint::Graphics, m_pso, m_rootSignature);
            encoder.SetConstantBuffer(RHIBindPoint::Graphics, 0, cb.gpuAddress);
            encoder.SetBindings(RHIBindPoint::Graphics, 1, srvTable);

            encoder.SetPrimitiveTopology(RHIPrimitiveTopology::TriangleList);
            encoder.Draw(3, 1);
        },
        m_keepAlive);
}

void EnhancedSSRPass::Shutdown()
{
    m_width = 0;
    m_height = 0;
    m_pso = nullptr;
    m_rootSignature = nullptr;
}

#endif
