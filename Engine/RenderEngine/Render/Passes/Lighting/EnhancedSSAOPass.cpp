#include "EnhancedSSAOPass.h"
#include "../../../RHI/DX12/DX12DeviceResources.h"
#include "../../../RHI/DX12/DX12PSOManager.h"
#include "../../../RHI/DX12/DX12RootSignatureCache.h"
#include "../../Graph/EnhancedRenderGraph.h"
#include "../../../RHI/RHIEncoder.h"

#include <sstream>
#include <cstddef>
#include "../../../RHI/RHIShaderCompiler.h"

// Half-resolution visibility bitmask AO followed by a depth-aware spatial filter.
// Inputs: device depth and encoded world normals. Output: (visibility, view Z).

namespace
{
    // 유니티 빌드에서 익명 네임스페이스가 합쳐지므로 이름을 고유하게 둔다.
    std::string SsaoHrToString(HRESULT hr)
    {
        std::ostringstream oss;
        oss << "HRESULT 0x" << std::hex << static_cast<unsigned long>(hr);
        return oss.str();
    }

    constexpr const char* kAOShaderFile = "SsaoAO.slang";
    constexpr const char* kFilterShaderFile = "SsaoFilter.slang";

    struct SSAOParams
    {
        math::matrix4x4 inverseProjection{ math::matrix4x4::identity() };
        math::matrix4x4 projection{ math::matrix4x4::identity() };
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
        math::matrix4x4 view{ math::matrix4x4::identity() };
    };
    static_assert(offsetof(SSAOParams, view) == 176);
    static_assert(sizeof(SSAOParams) == 240);

    bool CompileSsaoShader(const char* file,
        const RHIShaderPermutation& permutation,
        RHIShaderBlob& outBlob, std::string& outError)
    {
        // Shared constants and reconstruction live in Includes/SsaoCommon.slang.
        // 예전에는 문자열을 앞에 이어 붙였는데, 소스가 파일이 되면서
        // 인클루드 핸들러가 소스 파일 위치를 기준으로 풀어 준다.
        return RHIShaderCompiler::CompileFile(file, "CSMain", "cs_5_0", permutation,
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

    RHIShaderPermutation aoPermutation;
    if (!aoPermutation.Set("DIRECTIONS", directions, outError)
        || !aoPermutation.Set("STEPS", steps, outError)
        || !aoPermutation.Set("BITMASK_BITS", bits, outError))
        return false;

    RHIShaderBlob aoBlob;
    if (!CompileSsaoShader(kAOShaderFile, aoPermutation, aoBlob, outError)) return false;

    RHIComputePipelineDesc aoDesc{};
    aoDesc.csBytecode = aoBlob.Data();
    aoDesc.csSize = aoBlob.Size();
    aoDesc.layout = root;

    m_aoPSO = context.psoManager->GetOrCreateCompute(aoDesc, outError);
    if (!m_aoPSO.IsValid()) return false;


    // 필터 반경 1(3x3). 반해상도에서 3x3이면 전 해상도 6x6에 해당하고,
    // 그보다 넓히면 접촉 그림자가 뭉개진다 — 실측으로 바꿀 근거가 생기면 바꾼다.
    RHIShaderPermutation filterPermutation;
    if (!filterPermutation.Set("FILTER_RADIUS", "1", outError)) return false;

    RHIShaderBlob filterBlob;
    if (!CompileSsaoShader(kFilterShaderFile, filterPermutation,
            filterBlob, outError)) return false;

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

    constexpr uint32_t divisor = kResolutionDivisor;
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
            params.inverseProjection =
                math::transpose(context.camera->inverseProjection);
            params.projection = math::transpose(context.camera->projection);
            params.view = math::transpose(context.camera->view);
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
            const auto cb = context.resources->UploadConstants(
                &params, sizeof(SSAOParams));
            if (!cb.IsValid()) return;
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
                m_aoPSO);
            encoder.SetConstantBuffer(RHIBindPoint::Compute, 0, cb);
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
            const auto cb = context.resources->UploadConstants(
                &params, sizeof(SSAOParams));
            if (!cb.IsValid()) return;
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
            encoder.SetConstantBuffer(RHIBindPoint::Compute, 0, cb);
            encoder.SetBindings(RHIBindPoint::Compute, 1, srvTable);
            encoder.SetBindings(RHIBindPoint::Compute, 2, uavTable);

            encoder.Dispatch((m_width + 7) / 8, (m_height + 7) / 8, 1);
        }, m_keepAlive);
}

void EnhancedSSAOPass::Shutdown()
{
    m_width = 0;
    m_height = 0;
    m_frameIndex = 0;
    m_aoPSO = {};
    m_filterPSO = {};
}
