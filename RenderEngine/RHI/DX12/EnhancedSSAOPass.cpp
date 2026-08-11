#ifndef DYNAMICCPP_EXPORTS
#include "EnhancedSSAOPass.h"
#include "DX12DeviceResources.h"
#include "DX12PSOManager.h"
#include "DX12RootSignatureCache.h"
#include "EnhancedRenderGraph.h"
#include "RHIEncoder.h"

#include <sstream>
#include <vector>
#include "DX12ShaderCompiler.h"

// 단계(순서대로 채운다):
//   [v] 1. 반해상도 AO 컴퓨트 + 자가 검증
//   [v] 2. 디노이즈·업샘플
//   [v] 3. 실제 씬 연결 + 기존 SSAO와 시간 비교
//
// 자가 검증(dx12.ssao):
//   평평한 곳 0.969 · 계단 안쪽 0.373 · 필터 이웃 차이 51.7% 감소
// 시간 비교(dx12.ssaoscale, 1920x1080):
//   신규 0.196 ms · 참조(커널 64) 0.810 ms — 4.1배
// 실제 씬(dx12.scene, 256x256):
//   SSAO.Compute 0.0133 ms · SSAO.Filter 0.0020 ms
//
// AO는 SSGI 합성이 간접광에 곱해 쓴다. 직접광에 곱하면 광원이 실제로
// 보이는 곳까지 어두워져 그림자가 두 번 진 것처럼 보인다.

namespace
{
    // 유니티 빌드에서 익명 네임스페이스가 합쳐지므로 이름을 고유하게 둔다.
    std::string SsaoHrToString(HRESULT hr)
    {
        std::ostringstream oss;
        oss << "HRESULT 0x" << std::hex << static_cast<unsigned long>(hr);
        return oss.str();
    }

    // ── 공통 조각 ──
    //
    // 두 셰이더가 같은 깊이 → 뷰 위치 변환을 쓴다. 한 곳에 두는 이유는
    // 이 변환이 어긋나면 AO와 필터가 서로 다른 공간을 보게 되는데, 그 증상이
    // '그림이 조금 이상하다'로만 나타나기 때문이다.

    // ── AO 컴퓨트 ──
    //
    // 방향마다 화면 공간으로 걸으며 32비트 가시성 마스크를 세운다.
    //
    // 왜 비트마스크인가: 가중치를 누적하는 방식은 같은 각도를 두 번 세기
    // 쉽다. 앞뒤로 겹친 가림막 둘이 같은 방향을 막고 있으면 기여가 두 번
    // 들어가 AO가 실제보다 어두워지는데, 그 오차는 '좀 어둡다'로만 보여
    // 원인을 특정할 수 없다. 마스크는 이미 세워진 비트를 다시 세우지
    // 않으므로 그 실수가 구조적으로 불가능하다.
    constexpr const char* kAOShaderFile = "SsaoAO.hlsl";


    // ── 참조 경로: 기존 반구 커널 ──
    //
    // 성능 비교의 기준선. 기존 DX11 SSAO가 하던 것을 그대로 옮겼다 —
    // 커널 64개, 표본마다 클립 투영 한 번과 깊이 역투영 한 번.
    //
    // 실제 DX11 패스와 직접 재지 않는 이유는 그러면 API 차이가 수에 섞여
    // '무엇 때문에 빠른가'를 알 수 없기 때문이다. 같은 디바이스·같은 입력
    // 위에서 알고리즘만 갈아 끼운다.
    //
    // 커널은 셰이더 안에서 해시로 만든다. 상수 버퍼로 64개를 넘기던 원본과
    // 표본 분포는 다르지만, 재는 것은 '표본 하나당 비용'이라 분포는 결과에
    // 영향을 주지 않는다.
    constexpr const char* kReferenceShaderFile = "SsaoReference.hlsl";

    // ── 디노이즈 ──
    //
    // 교차 양방향 필터. 깊이가 비슷한 이웃만 섞어 경계를 지킨다.
    //
    // 노멀을 안 쓰는 이유: AO는 이미 노멀을 반영한 값이고, 여기서 다시
    // 노멀로 가중치를 주면 같은 정보를 두 번 쓰는 셈이다. 깊이만으로
    // 부족하다는 실측이 나오면 그때 넣는다.
    constexpr const char* kFilterShaderFile = "SsaoFilter.hlsl";

    struct SSAOParams
    {
        Mathf::Matrix inverseProjection{};
        Mathf::Matrix projection{};
        uint32_t      sizeX{ 0 };
        uint32_t      sizeY{ 0 };
        uint32_t      fullSizeX{ 0 };
        uint32_t      fullSizeY{ 0 };
        float         radius{ 0.f };
        float         thickness{ 0.f };
        float         intensity{ 0.f };
        float         depthSigma{ 0.f };
        uint32_t      frameIndex{ 0 };
        uint32_t      pad[3]{};
    };

    bool CompileSsaoShader(const char* file, const D3D_SHADER_MACRO* defines,
        RHIShaderBlob& outBlob, std::string& outError)
    {
        // 공통 조각은 셰이더가 #include "SsaoCommon.hlsli" 로 직접 당긴다.
        // 예전에는 문자열을 앞에 이어 붙였는데, 소스가 파일이 되면서
        // 인클루드 핸들러가 소스 파일 위치를 기준으로 풀어 준다.
        return DX12ShaderCompiler::CompileFile(file, "CSMain", "cs_5_0", defines,
            outBlob, outError);
    }
}

bool EnhancedSSAOPass::Initialize(const EnhancedFrameContext& context, std::string& outError)
{
    if (nullptr == context.resources || nullptr == context.psoManager ||
        nullptr == context.rootSignatures)
    {
        outError = "SSAO 패스 컨텍스트가 불완전하다";
        return false;
    }

    return CreatePipelines(context, outError);
}

bool EnhancedSSAOPass::CreatePipelines(const EnhancedFrameContext& context, std::string& outError)
{
    // 두 셰이더가 같은 시그니처를 쓴다. AO는 SRV 둘, 필터는 하나지만
    // 넓은 쪽에 얹어도 비용이 없고, 시그니처를 나누면 패스 사이에서
    // 그것을 바꾸는 비용이 더 든다(SSGI와 같은 판단이다).
    const RHIPipelineLayoutParam params[] = {
        RHILayout::Cbv(0),
        RHILayout::SrvTable(2, 0),
        RHILayout::UavTable(1, 0),
    };

    RHIPipelineLayoutDesc rootDesc{};
    rootDesc.params = params;

    const auto root = context.rootSignatures->GetOrCreate(rootDesc, outError);
    if (!root.IsValid()) return false;

    const std::string directions = std::to_string(kDirectionsPerPixel);
    const std::string steps = std::to_string(kStepsPerDirection);
    const std::string bits = std::to_string(kBitmaskBits);

    const D3D_SHADER_MACRO aoDefines[] = {
        { "DIRECTIONS", directions.c_str() },
        { "STEPS", steps.c_str() },
        { "BITMASK_BITS", bits.c_str() },
        { nullptr, nullptr },
    };

    RHIShaderBlob aoBlob;
    if (!CompileSsaoShader(kAOShaderFile, aoDefines, aoBlob, outError)) return false;

    RHIComputePipelineDesc aoDesc{};
    aoDesc.csBytecode = aoBlob.Data();
    aoDesc.csSize = aoBlob.Size();
    aoDesc.layout = root;

    m_aoPSO = context.psoManager->GetOrCreateCompute(aoDesc, outError);
    if (!m_aoPSO.IsValid()) return false;

    RHIShaderBlob refBlob;
    if (!CompileSsaoShader(kReferenceShaderFile, nullptr, refBlob, outError)) return false;

    RHIComputePipelineDesc refDesc{};
    refDesc.csBytecode = refBlob.Data();
    refDesc.csSize = refBlob.Size();
    refDesc.layout = root;

    m_referencePSO = context.psoManager->GetOrCreateCompute(refDesc, outError);
    if (!m_referencePSO.IsValid()) return false;

    // 필터 반경 1(3x3). 반해상도에서 3x3이면 전 해상도 6x6에 해당하고,
    // 그보다 넓히면 접촉 그림자가 뭉개진다 — 실측으로 바꿀 근거가 생기면 바꾼다.
    const D3D_SHADER_MACRO filterDefines[] = {
        { "FILTER_RADIUS", "1" },
        { nullptr, nullptr },
    };

    RHIShaderBlob filterBlob;
    if (!CompileSsaoShader(kFilterShaderFile, filterDefines, filterBlob, outError)) return false;

    RHIComputePipelineDesc filterDesc{};
    filterDesc.csBytecode = filterBlob.Data();
    filterDesc.csSize = filterBlob.Size();
    filterDesc.layout = root;

    m_filterPSO = context.psoManager->GetOrCreateCompute(filterDesc, outError);
    return m_filterPSO.IsValid();
}

bool EnhancedSSAOPass::PrepareFrame(const EnhancedFrameContext& context, std::string& outError)
{
    (void)outError;

    // 참조 경로는 전 해상도로 돈다. 반해상도는 새 방식이 가져가는 이득의
    // 일부라, 기준선에까지 적용하면 그 이득이 수에서 사라진다.
    const uint32_t divisor = m_useReferencePath ? 1u : kResolutionDivisor;
    m_width = (context.width + divisor - 1) / divisor;
    m_height = (context.height + divisor - 1) / divisor;
    return true;
}

void EnhancedSSAOPass::Declare(EnhancedRenderGraph& graph, const EnhancedFrameContext& context)
{
    m_output = RGHandle{};
    m_rawOutput = RGHandle{};

    if (!m_inputs.depth.IsValid() || !m_inputs.normal.IsValid() ||
        !m_aoPSO.IsValid() || !m_filterPSO.IsValid() ||
        0 == m_width || 0 == m_height)
    {
        return;
    }

    RGTextureDesc aoDesc{};
    aoDesc.width = m_width;
    aoDesc.height = m_height;
    aoDesc.format = kAOFormat;
    aoDesc.allowUnorderedAccess = true;
    aoDesc.name = "SSAO.Raw";
    m_rawOutput = graph.CreateTexture(aoDesc);

    RGTextureDesc filteredDesc = aoDesc;
    filteredDesc.name = "SSAO.Filtered";
    m_output = graph.CreateTexture(filteredDesc);

    // 상수는 두 패스가 같은 것을 쓴다. 한 번 만들어 둘 다 가리키게 하면
    // 값이 갈릴 자리가 없어진다 — SSGI에서 크기 상수를 패스마다 따로
    // 채우다가 필터가 다른 해상도를 본 적이 있다.
    const auto fillParams = [this, &context]() -> SSAOParams
    {
        SSAOParams params{};
        if (nullptr != context.camera)
        {
            params.inverseProjection = XMMatrixTranspose(
                XMMatrixInverse(nullptr, context.camera->projection));
            params.projection = XMMatrixTranspose(context.camera->projection);
        }
        params.sizeX = m_width;
        params.sizeY = m_height;
        params.fullSizeX = context.width;
        params.fullSizeY = context.height;
        params.radius = m_tuning.radius;
        params.thickness = m_tuning.thickness;
        params.intensity = m_tuning.intensity;
        params.depthSigma = m_tuning.filterDepthSigma;
        params.frameIndex = m_frameIndex;
        return params;
    };

    // ── AO ──
    graph.AddPass("SSAO.Compute",
        { { m_inputs.depth, RHIResourceState::ShaderResource },
          { m_inputs.normal, RHIResourceState::ShaderResource },
          { m_rawOutput, RHIResourceState::UnorderedAccess } },
        [this, &context, fillParams](const EnhancedRenderGraph::ExecuteContext& executeContext)
        {

            const SSAOParams params = fillParams();
            const auto cb = context.resources->GetUploadRing().Allocate(
                sizeof(SSAOParams), DX12UploadRing::kConstantBufferAlignment);
            if (!cb.IsValid()) return;
            memcpy(cb.cpuAddress, &params, sizeof(params));

            // 테이블 둘을 잘라 받는다(R2) — 루트 파라미터가 SRV·UAV로 나뉘어 있다.
            const RHIBindingDesc srvs[] = {
                RHIBindingDesc::SrvDepth(executeContext.ResolveHandle(m_inputs.depth)),
                RHIBindingDesc::Srv(executeContext.ResolveHandle(m_inputs.normal)),
            };
            const RHIBindingDesc uavs[] = {
                RHIBindingDesc::Uav2D(executeContext.ResolveHandle(m_rawOutput), kAOFormat),
            };
            const RHIBindingTable srvTable = context.resources->CreateBindings(srvs);
            const RHIBindingTable uavTable = context.resources->CreateBindings(uavs);
            if (!srvTable.IsValid() || !uavTable.IsValid()) return;

            RHIEncoder& encoder = *executeContext.encoder;
            encoder.SetPipeline(RHIBindPoint::Compute,
                m_useReferencePath ? m_referencePSO : m_aoPSO);
            encoder.SetConstantBuffer(RHIBindPoint::Compute, 0, cb.gpuAddress);
            encoder.SetBindings(RHIBindPoint::Compute, 1, srvTable);
            encoder.SetBindings(RHIBindPoint::Compute, 2, uavTable);

            encoder.Dispatch((m_width + 7) / 8, (m_height + 7) / 8, 1);
        });

    // ── 디노이즈 ──
    graph.AddPass("SSAO.Filter",
        { { m_rawOutput, RHIResourceState::ShaderResource },
          { m_output, RHIResourceState::UnorderedAccess } },
        [this, &context, fillParams](const EnhancedRenderGraph::ExecuteContext& executeContext)
        {

            const SSAOParams params = fillParams();
            const auto cb = context.resources->GetUploadRing().Allocate(
                sizeof(SSAOParams), DX12UploadRing::kConstantBufferAlignment);
            if (!cb.IsValid()) return;
            memcpy(cb.cpuAddress, &params, sizeof(params));

            // ★ SRV 슬롯 수는 고정이다.
            //
            // 필터는 t0 하나만 쓰지만 테이블은 둘을 자른다. 조건에 따라
            // 슬롯 수를 바꾸면 레지스터가 밀리는데, SSGI에서 그것으로
            // 누적이 조용히 죽은 적이 있다. 안 쓰는 슬롯도 같은 것으로
            // 채워 두면 디스크립터 힙에 쓰레기가 남지 않는다.
            const RHITextureHandle raw = executeContext.ResolveHandle(m_rawOutput);
            const RHIBindingDesc srvs[] = {
                RHIBindingDesc::Srv2D(raw, kAOFormat),
                RHIBindingDesc::Srv2D(raw, kAOFormat),
            };
            const RHIBindingDesc uavs[] = {
                RHIBindingDesc::Uav2D(executeContext.ResolveHandle(m_output), kAOFormat),
            };
            const RHIBindingTable srvTable = context.resources->CreateBindings(srvs);
            const RHIBindingTable uavTable = context.resources->CreateBindings(uavs);
            if (!srvTable.IsValid() || !uavTable.IsValid()) return;

            RHIEncoder& encoder = *executeContext.encoder;
            encoder.SetPipeline(RHIBindPoint::Compute, m_filterPSO);
            encoder.SetConstantBuffer(RHIBindPoint::Compute, 0, cb.gpuAddress);
            encoder.SetBindings(RHIBindPoint::Compute, 1, srvTable);
            encoder.SetBindings(RHIBindPoint::Compute, 2, uavTable);

            encoder.Dispatch((m_width + 7) / 8, (m_height + 7) / 8, 1);
        });
}

void EnhancedSSAOPass::Shutdown()
{
    m_width = 0;
    m_height = 0;
    m_frameIndex = 0;
    m_aoPSO = {};
    m_referencePSO = {};
    m_filterPSO = {};
}

#endif
