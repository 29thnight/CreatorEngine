#ifndef DYNAMICCPP_EXPORTS
#include "EnhancedSSSPass.h"
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
    std::string SssHrToString(HRESULT hr)
    {
        std::ostringstream oss;
        oss << "HRESULT 0x" << std::hex << static_cast<unsigned long>(hr);
        return oss.str();
    }

    // DX11 SSS.ps.hlsl의 이식. 25샘플 커널과 표면 추종 수식을 그대로 옮겼다 —
    // 그림의 기준선이므로 상수 하나도 건드리지 않는다.
    //
    // 바꾼 것은 셋뿐이다:
    //   · 정점을 SV_VertexID 풀스크린 삼각형으로(DX11은 Fullscreen.vs + Draw(4))
    //   · MetalRough(t2) 선언 제거 — 원본이 읽지 않는다
    //   · direction을 상수로 받되 호출부가 축을 고정한다(원본과 같은 동작)
    constexpr const char* kSSSShader = R"(
Texture2D    gDepth   : register(t0);
Texture2D    gColor   : register(t1);
SamplerState gSampler : register(s0);

cbuffer SSSConstants : register(b0)
{
    float2 gDirection;
    float  gStrength;
    float  gWidth;
    float  gCameraFov;
    float3 gPadding;
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

static const int NUM_SAMPLES = 25;

static const float4 kernel[NUM_SAMPLES] =
{
    float4(0.530605,    0.613514,       0.739601,        0),
    float4(0.000973794, 1.11862e-005,   9.43437e-007,   -3),
    float4(0.00333804,  7.85443e-005,   1.2945e-005,    -2.52083),
    float4(0.00500364,  0.00020094,     5.28848e-005,   -2.08333),
    float4(0.00700976,  0.00049366,     0.000151938,    -1.6875),
    float4(0.0094389,   0.00139119,     0.000416598,    -1.33333),
    float4(0.0128496,   0.00356329,     0.00132016,     -1.02083),
    float4(0.017924,    0.00711691,     0.00347194,     -0.75),
    float4(0.0263642,   0.0119715,      0.00684598,     -0.520833),
    float4(0.0410172,   0.0199899,      0.0118481,      -0.333333),
    float4(0.0493588,   0.0367726,      0.0219485,      -0.1875),
    float4(0.0402784,   0.0657244,      0.04631,        -0.0833333),
    float4(0.0211412,   0.0459286,      0.0378196,      -0.0208333),
    float4(0.0211412,   0.0459286,      0.0378196,       0.0208333),
    float4(0.0402784,   0.0657244,      0.04631,         0.0833333),
    float4(0.0493588,   0.0367726,      0.0219485,       0.1875),
    float4(0.0410172,   0.0199899,      0.0118481,       0.333333),
    float4(0.0263642,   0.0119715,      0.00684598,      0.520833),
    float4(0.017924,    0.00711691,     0.00347194,      0.75),
    float4(0.0128496,   0.00356329,     0.00132016,      1.02083),
    float4(0.0094389,   0.00139119,     0.000416598,     1.33333),
    float4(0.00700976,  0.00049366,     0.000151938,     1.6875),
    float4(0.00500364,  0.00020094,     5.28848e-005,    2.08333),
    float4(0.00333804,  7.85443e-005,   1.2945e-005,     2.52083),
    float4(0.000973794, 1.11862e-005,   9.43437e-007,    3)
};

float4 PSMain(VSOut input) : SV_TARGET
{
    const float4 colorM = gColor.SampleLevel(gSampler, input.texCoord, 0);
    const float  depthM = gDepth.SampleLevel(gSampler, input.texCoord, 0).r;

    // 투영창까지의 거리. FOV를 도(degree)로 받는 것까지 원본 그대로다.
    const float distanceToProjectionWindow = 1.0f / tan(0.5f * gCameraFov * 3.141592f / 180.0f);
    const float scale = distanceToProjectionWindow / depthM;

    float2 finalStep = gWidth * scale * gDirection;
    finalStep *= gStrength;
    finalStep *= 0.333f;   // 커널이 -3~3이라 3으로 나눈다

    float4 colorBlurred = colorM;
    colorBlurred.rgb *= kernel[0].rgb;

    [unroll]
    for (int i = 1; i < NUM_SAMPLES; i++)
    {
        const float2 offset = input.texCoord + kernel[i].a * finalStep;
        float4 color = gColor.SampleLevel(gSampler, offset, 0);

        // 표면 추종 — 깊이가 크게 다르면 중심 색으로 되돌린다. 이것이 없으면
        // 실루엣 밖으로 피부색이 번진다.
        {
            const float depth = gDepth.SampleLevel(gSampler, offset, 0).r;
            const float s = saturate(300.0f * distanceToProjectionWindow * gWidth
                * abs(depthM - depth));
            color.rgb = lerp(color.rgb, colorM.rgb, s);
        }

        colorBlurred.rgb += kernel[i].rgb * color.rgb;
    }

    return colorBlurred;
}
)";

    struct SSSConstants
    {
        float direction[2]{};
        float strength{ 0.f };
        float width{ 0.f };
        float cameraFov{ 0.f };
        float padding[3]{};
    };

    bool CompileSSSShader(const char* entry, const char* target,
        Microsoft::WRL::ComPtr<ID3DBlob>& outBlob, std::string& outError)
    {
        Microsoft::WRL::ComPtr<ID3DBlob> errors;
        const HRESULT hr = D3DCompile(kSSSShader, strlen(kSSSShader), nullptr, nullptr,
            nullptr, entry, target, 0, 0, &outBlob, &errors);
        if (FAILED(hr))
        {
            outError = std::string("SSS 셰이더 컴파일 실패(") + entry + "): ";
            if (errors) outError += static_cast<const char*>(errors->GetBufferPointer());
            else        outError += SssHrToString(hr);
            return false;
        }
        return true;
    }
}

bool EnhancedSSSPass::Initialize(const EnhancedFrameContext& context, std::string& outError)
{
    if (nullptr == context.resources || nullptr == context.psoManager ||
        nullptr == context.rootSignatures)
    {
        outError = "SSS 패스 컨텍스트가 불완전하다";
        return false;
    }

    return CreatePipelines(context, outError);
}

bool EnhancedSSSPass::CreatePipelines(const EnhancedFrameContext& context, std::string& outError)
{
    // b0 상수 · t0~t1 테이블(깊이·색) · s0 선형 클램프.
    //
    // 클램프가 중요하다 — 커널이 화면 밖을 짚을 때 WRAP이면 반대편 색이
    // 딸려 와 가장자리에 엉뚱한 번짐이 생긴다.
    const RHIPipelineLayoutParam params[] = {
        RHILayout::Cbv(0, RHIShaderVisibility::Pixel),
        RHILayout::SrvTable(2, 0, RHIShaderVisibility::Pixel),
    };

    const RHIStaticSamplerDesc samplers[] = {
        { RHISampler::Linear(RHIAddressMode::Clamp), 0, RHIShaderVisibility::Pixel },
    };

    RHIPipelineLayoutDesc rootDesc{};
    rootDesc.params = params;
    rootDesc.staticSamplers = samplers;

    const auto root = context.rootSignatures->GetOrCreate(rootDesc, outError);
    if (!root.IsValid()) return false;
    m_rootSignature = root.signature;

    ComPtr<ID3DBlob> vsBlob;
    ComPtr<ID3DBlob> psBlob;
    if (!CompileSSSShader("VSMain", "vs_5_0", vsBlob, outError)) return false;
    if (!CompileSSSShader("PSMain", "ps_5_0", psBlob, outError)) return false;

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

    // ★ 숨어 있던 암묵 상태를 명시한다.
    //
    // DX11은 이 패스에서 블렌드·깊이 상태를 세우지 않고 앞 패스가 남긴
    // 것에 얹혀 갔다. DX12는 PSO에 박아야 하므로 결정해야 하고, 블러
    // 결과는 덮어쓰는 것이 맞다(깊이도 안 본다 — 풀스크린이다).
    desc.depthEnable = false;
    desc.blendEnable = false;
    desc.cullMode = D3D12_CULL_MODE_NONE;
    desc.numRenderTargets = 1;
    desc.rtvFormats[0] = ToDXGI(kOutputFormat);

    m_pso = context.psoManager->GetOrCreate(desc, outError);
    if (nullptr == m_pso) return false;

    return true;
}

bool EnhancedSSSPass::PrepareFrame(const EnhancedFrameContext& context, std::string& outError)
{
    (void)outError;

    m_width = context.width;
    m_height = context.height;

    // FOV는 도로 넘긴다 — 셰이더가 그렇게 받는다(원본 그대로).
    m_cameraFov = (nullptr != context.camera)
        ? XMConvertToDegrees(context.camera->fov) : 60.f;

    return true;
}

void EnhancedSSSPass::Declare(EnhancedRenderGraph& graph, const EnhancedFrameContext& context)
{
    // 꺼져 있으면 입력을 그대로 흘린다. 뒤 패스가 '켜졌나'를 따지지 않고
    // GetOutput()만 이으면 되도록 — SSR과 같은 규약이다.
    if (!m_enabled)
    {
        m_horizontal = RGHandle{};
        m_output = m_inputs.color;
        return;
    }

    m_output = RGHandle{};
    m_horizontal = RGHandle{};

    if (nullptr == m_pso || 0 == m_width || 0 == m_height) return;
    if (!m_inputs.color.IsValid() || !m_inputs.depth.IsValid()) return;

    // ★ 복사가 사라진 자리.
    //
    // DX11은 읽으면서 쓸 수 없어 매 축마다 씬 컬러를 통째로 복사했다
    // (화면 크기 CopyResource 2회). 여기서는 가로 블러가 transient에 쓰고
    // 세로 블러가 그것을 읽으므로 복사가 필요 없다.
    RGTextureDesc desc{};
    desc.width = m_width;
    desc.height = m_height;
    desc.format = ToDXGI(kOutputFormat);
    desc.allowRenderTarget = true;

    desc.name = "SSS.Horizontal";
    m_horizontal = graph.CreateTexture(desc);

    desc.name = "SSS.Output";
    m_output = graph.CreateTexture(desc);

    // 두 축을 각각 선언한다. 그래프가 사이의 전이 배리어를 만들어 준다 —
    // 가로가 쓴 것을 세로가 읽으므로 RENDER_TARGET → SHADER_RESOURCE다.
    // isFinal은 예전에 rtvIndex가 겸하던 판단이다 — 힙 슬롯 번호가 '마지막
    // 축인가'까지 뜻하고 있었다. R2b가 슬롯을 걷어내면서 그 겸직이 드러나
    // 뜻하는 바를 그대로 적었다.
    const auto declareAxis = [&](RGHandle source, RGHandle target,
        bool isFinal, float dirX, float dirY, const char* name)
    {
        const std::vector<EnhancedRenderGraph::RGPassUsage> usages = {
            { source,         RHIResourceState::ShaderResource },
            { m_inputs.depth, RHIResourceState::ShaderResource },
            { target,         RHIResourceState::RenderTarget },
        };

        graph.AddPass(name, usages,
            [this, &context, source, target, dirX, dirY](
                const EnhancedRenderGraph::ExecuteContext& executeContext)
            {
                RHIEncoder& encoder = *executeContext.encoder;

                const RHITextureHandle colors[] = { executeContext.ResolveHandle(target) };
                const auto targets = context.resources->CreateRenderTargets(colors);
                if (!targets.IsValid()) return;

                encoder.SetViewportAndScissor(m_width, m_height);
                encoder.BindRenderTargets(targets);

                // 테이블 하나로 잘라 받는다(R2). 깊이는 D32_FLOAT 리소스를
                // SRV로 읽는 것이라 포맷을 R32_FLOAT로 명시해야 하고,
                // 색은 리소스가 아는 대로 보면 된다.
                const RHIBindingDesc bindings[] = {
                    RHIBindingDesc::Srv2D(executeContext.ResolveHandle(m_inputs.depth),
                        DXGI_FORMAT_R32_FLOAT),
                    RHIBindingDesc::Srv(executeContext.ResolveHandle(source)),
                };
                const RHIBindingTable srvTable = context.resources->CreateBindings(bindings);
                if (!srvTable.IsValid()) return;

                SSSConstants constants{};
                constants.direction[0] = dirX;
                constants.direction[1] = dirY;
                constants.strength = m_tuning.strength;
                constants.width = m_tuning.width;
                constants.cameraFov = m_cameraFov;

                const auto cb = context.resources->GetUploadRing().Allocate(
                    sizeof(SSSConstants), DX12UploadRing::kConstantBufferAlignment);
                if (!cb.IsValid()) return;
                memcpy(cb.cpuAddress, &constants, sizeof(constants));

                encoder.SetPipeline(RHIBindPoint::Graphics, m_pso, m_rootSignature);
                encoder.SetConstantBuffer(RHIBindPoint::Graphics, 0, cb.gpuAddress);
                encoder.SetBindings(RHIBindPoint::Graphics, 1, srvTable);

                encoder.SetPrimitiveTopology(RHIPrimitiveTopology::TriangleList);
                encoder.Draw(3, 1);
            },
            // 가로는 세로가 읽으므로 뿌리가 아니어도 살아남는다. 세로는
            // 소비자가 붙기 전까지 호출부가 정한다.
            isFinal ? m_keepAlive : false);
    };

    // 축은 고정이다. DX11의 direction 슬라이더는 코드가 항상 덮어써
    // 죽어 있었고, 분리 블러는 축이 고정이어야 맞다.
    declareAxis(m_inputs.color, m_horizontal, false, 1.f, 0.f, "SSS.Horizontal");
    declareAxis(m_horizontal, m_output, true, 0.f, 1.f, "SSS.Vertical");
}

void EnhancedSSSPass::Shutdown()
{
    m_width = 0;
    m_height = 0;
    m_pso = nullptr;
    m_rootSignature = nullptr;
}

#endif
