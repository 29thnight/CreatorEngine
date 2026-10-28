#include "../../../../Containers/ContainerAPI.h"
#include "EnhancedSSAOPass.h"
#include "../../../RHI/RHIEncoder.h"
#include "../../../RHI/RHIShaderCompiler.h"

namespace
{
    struct SSAOParams
    {
        math::matrix4x4 inverseProjection{math::matrix4x4::identity()};
        math::matrix4x4 projection{math::matrix4x4::identity()};
        uint32_t sizeX{}, sizeY{}, fullSizeX{}, fullSizeY{};
        float radius{}, thickness{}, intensity{}, depthSigma{};
        uint32_t frameIndex{}, pad[3]{};
        math::matrix4x4 view{math::matrix4x4::identity()};
    };
    static_assert(sizeof(SSAOParams) == 240);
    static_assert(offsetof(SSAOParams, view) == 176);

    struct SSAOTemporalParams
    {
        math::matrix4x4 inverseProjection{math::matrix4x4::identity()};
        math::matrix4x4 inverseView{math::matrix4x4::identity()};
        math::matrix4x4 previousViewProjection{math::matrix4x4::identity()};
        math::matrix4x4 previousView{math::matrix4x4::identity()};
        uint32_t width{}, height{}, fullWidth{}, fullHeight{};
        uint32_t hasHistory{}, hasMotion{}, maxFrames{}, pad{};
        float depthTolerance{}, normalThreshold{}, jitterX{}, jitterY{};
    };
    static_assert(sizeof(SSAOTemporalParams) == 304);
} // namespace

void EnhancedSSAOPass::SetTuning(const Tuning& tuning)
{
    if (m_tuning.radius != tuning.radius || m_tuning.thickness != tuning.thickness ||
        m_tuning.intensity != tuning.intensity || m_tuning.filterDepthSigma != tuning.filterDepthSigma ||
        m_tuning.maxHistoryFrames != tuning.maxHistoryFrames ||
        m_tuning.historyDepthTolerance != tuning.historyDepthTolerance ||
        m_tuning.historyNormalThreshold != tuning.historyNormalThreshold ||
        m_tuning.temporalEnabled != tuning.temporalEnabled)
    {
        ResetHistory();
    }
    m_tuning = tuning;
}

bool EnhancedSSAOPass::Initialize(const EnhancedFrameContext& context, std::string& outError)
{
    if (!context.resources || !context.psoManager || !context.rootSignatures)
    {
        outError = "SSAO requires device, pipeline and root signature services.";
        return false;
    }
    return CreatePipelines(context, outError);
}

bool EnhancedSSAOPass::CreatePipelines(const EnhancedFrameContext& context, std::string& outError)
{
    const auto create =
        [&](const char* file, const RHIShaderPermutation& permutation, bool temporal, RHIPipelineHandle& output)
    {
        const RHIPipelineLayoutParam params[]{RHILayout::Cbv(0), RHILayout::SrvTable(temporal ? 8 : 2, 0),
                                              RHILayout::UavTable(temporal ? 3 : 1, 0)};
        RHIPipelineLayoutDesc rootDesc{};
        rootDesc.params = params;
        const auto root = context.rootSignatures->GetOrCreate(rootDesc, outError);
        RHIShaderBlob blob;
        if (!root.IsValid() || !RHIShaderCompiler::CompileFile(file, "CSMain", "cs_5_0", permutation, blob, outError))
        {
            return false;
        }
        RHIComputePipelineDesc desc{};
        desc.csBytecode = blob.Data();
        desc.csSize = blob.Size();
        desc.layout = root;
        output = context.psoManager->GetOrCreateCompute(desc, outError);
        return output.IsValid();
    };
    RHIShaderPermutation ao;
    if (!ao.Set("DIRECTIONS", std::to_string(kDirectionsPerPixel), outError) ||
        !ao.Set("STEPS", std::to_string(kStepsPerDirection), outError) ||
        !ao.Set("BITMASK_BITS", std::to_string(kBitmaskBits), outError))
    {
        return false;
    }
    RHIShaderPermutation filter;
    if (!filter.Set("FILTER_RADIUS", "2", outError))
    {
        return false;
    }
    return create("SsaoAO.slang", ao, false, m_aoPSO) && create("SsaoTemporal.slang", {}, true, m_temporalPSO) &&
           create("SsaoFilter.slang", filter, false, m_filterPSO);
}

void EnhancedSSAOPass::ResetHistory()
{
    ++m_historyGeneration;
    m_historyValid = false;
    m_historyIndex = 0;
}

bool EnhancedSSAOPass::EnsureHistory(const EnhancedFrameContext& context, std::string& outError)
{
    if (!m_resourceRetirement.Attach(*context.resources))
    {
        outError = "SSAO history cannot change its owning device.";
        return false;
    }
    bool complete = true;
    for (uint32_t i = 0; i < kHistoryCount; ++i)
    {
        for (const auto texture : {m_history[i], m_geometryHistory[i]})
        {
            if (!texture.IsValid())
            {
                complete = false;
                continue;
            }
            const auto info = context.resources->DescribeTexture(texture);
            complete = complete && texture.IsValid() && info.width == m_width && info.height == m_height;
        }
    }
    if (complete)
    {
        return true;
    }
    ce::fixed_array<RHITextureHandle, kHistoryCount> history{}, geometry{};
    RHITextureDesc desc{};
    desc.width = m_width;
    desc.height = m_height;
    desc.allowUnorderedAccess = true;
    desc.initialState = RHIResourceState::ShaderResource;
    for (uint32_t i = 0; i < kHistoryCount; ++i)
    {
        desc.format = RHIFormat::RGBA16Float;
        desc.debugName = i == 0 ? L"SSAO.History0" : L"SSAO.History1";
        bool ok = context.resources->CreateTexture(desc, history[i], outError);
        if (ok)
        {
            // Store full-precision view depth so distant geometry is not accepted
            // or rejected according to half-float depth quantization.
            desc.format = RHIFormat::RGBA32Float;
            desc.debugName = i == 0 ? L"SSAO.GeometryHistory0" : L"SSAO.GeometryHistory1";
            ok = context.resources->CreateTexture(desc, geometry[i], outError);
        }
        if (!ok)
        {
            for (uint32_t j = 0; j < kHistoryCount; ++j)
            {
                if (history[j].IsValid())
                {
                    context.resources->ReleaseTexture(history[j]);
                }
                if (geometry[j].IsValid())
                {
                    context.resources->ReleaseTexture(geometry[j]);
                }
            }
            return false;
        }
    }
    for (uint32_t i = 0; i < kHistoryCount; ++i)
    {
        m_resourceRetirement.Retire(m_history[i]);
        m_resourceRetirement.Retire(m_geometryHistory[i]);
    }
    m_history = history;
    m_geometryHistory = geometry;
    m_historyState.fill(RHIResourceState::ShaderResource);
    m_geometryState.fill(RHIResourceState::ShaderResource);
    ResetHistory();
    return true;
}

bool EnhancedSSAOPass::PrepareFrame(const EnhancedFrameContext& context, std::string& outError)
{
    if (m_fullWidth != context.width || m_fullHeight != context.height)
    {
        ResetHistory();
    }
    m_fullWidth = context.width;
    m_fullHeight = context.height;
    m_width = (context.width + kResolutionDivisor - 1) / kResolutionDivisor;
    m_height = (context.height + kResolutionDivisor - 1) / kResolutionDivisor;
    if (context.temporalFrame.realFrameId && context.temporalFrame.reset)
    {
        ResetHistory();
    }
    return m_width && m_height && context.resources && EnsureHistory(context, outError);
}

void EnhancedSSAOPass::Declare(EnhancedRenderGraph& graph, const EnhancedFrameContext& context)
{
    m_output = m_rawOutput = m_resolved = m_historyOutput = {};
    if (!m_inputs.depth.IsValid() || !m_inputs.normal.IsValid() || !m_aoPSO.IsValid() || !m_temporalPSO.IsValid() ||
        !m_filterPSO.IsValid() || !m_width || !m_height)
    {
        ResetHistory();
        return;
    }
    const bool versioned = graph.GetSchedulingMode() == RGSchedulingMode::ExplicitVersioned;
    const bool explicitAccess = graph.GetSchedulingMode() != RGSchedulingMode::DeclarationOrder;
    const auto read = explicitAccess ? RGAccessMode::Read : RGAccessMode::LegacyState;
    const auto write = explicitAccess ? RGAccessMode::Write : RGAccessMode::LegacyState;
    const auto texture = [&](const char* name)
    {
        RGTextureDesc desc{};
        desc.width = m_width;
        desc.height = m_height;
        desc.format = kAOFormat;
        desc.allowUnorderedAccess = true;
        desc.name = name;
        auto handle = graph.CreateTexture(desc);
        return versioned ? graph.Write(handle) : handle;
    };
    m_rawOutput = texture("SSAO.Raw");
    m_resolved = texture("SSAO.Resolved");
    m_output = texture("SSAO.Filtered");

    SSAOParams params{};
    if (context.camera)
    {
        params.inverseProjection = math::transpose(context.camera->inverseProjection);
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

    // All execute data is copied into graph-owned arena storage. Neither an
    // asynchronous recording nor a later frame observes mutable pass handles.
    const auto dispatch = [&]<typename Params>(const char* name, RHIPipelineHandle pipeline, const Params& constants,
                                               std::span<const RGHandle> srvs, std::span<const RGHandle> uavs,
                                               std::span<const RHIFormat> formats, bool keepAlive,
                                               bool computeCompatible)
    {
        ce::fixed_array<EnhancedRenderGraph::RGPassUsage, 11> usages{};
        ce::fixed_array<RGHandle, 8> inputs{};
        ce::fixed_array<RGHandle, 3> outputs{};
        ce::fixed_array<RHIFormat, 3> outputFormats{};
        size_t count{};
        for (size_t i = 0; i < srvs.size(); ++i)
        {
            inputs[i] = srvs[i];
            usages[count++] = {srvs[i], RHIResourceState::ShaderResource, read};
        }
        for (size_t i = 0; i < uavs.size(); ++i)
        {
            outputs[i] = uavs[i];
            outputFormats[i] = formats[i];
            usages[count++] = {uavs[i], RHIResourceState::UnorderedAccess, write};
        }
        const auto pass = graph.AddPass(
            name, std::span{usages}.first(count),
            graph.StoreExecute(
                [device = context.resources, pipeline, constants, inputs, outputs, outputFormats,
                 inputCount = srvs.size(), outputCount = uavs.size(), width = m_width,
                 height = m_height](const EnhancedRenderGraph::ExecuteContext& execute)
                {
                    ce::fixed_array<RHIBindingDesc, 8> inputBindings{};
                    ce::fixed_array<RHIBindingDesc, 3> outputBindings{};
                    for (size_t i = 0; i < inputCount; ++i)
                    {
                        const auto handle = execute.ResolveHandle(inputs[i]);
                        const auto info = device->DescribeTexture(handle);
                        inputBindings[i] = info.format == RHIFormat::D32Float ? RHIBindingDesc::SrvDepth(handle)
                                                                              : RHIBindingDesc::Srv(handle);
                    }
                    for (size_t i = 0; i < outputCount; ++i)
                    {
                        outputBindings[i] = RHIBindingDesc::Uav2D(execute.ResolveHandle(outputs[i]), outputFormats[i]);
                    }
                    const auto cb = device->UploadConstants(&constants, sizeof(constants));
                    const auto srvTable = device->CreateBindings(std::span{inputBindings}.first(inputCount));
                    const auto uavTable = device->CreateBindings(std::span{outputBindings}.first(outputCount));
                    if (!cb.IsValid() || !srvTable.IsValid() || !uavTable.IsValid())
                    {
                        throw std::runtime_error("SSAO dispatch bindings could not be allocated.");
                    }
                    auto& encoder = *execute.encoder;
                    encoder.SetPipeline(RHIBindPoint::Compute, pipeline);
                    encoder.SetConstantBuffer(RHIBindPoint::Compute, 0, cb);
                    encoder.SetBindings(RHIBindPoint::Compute, 1, srvTable);
                    encoder.SetBindings(RHIBindPoint::Compute, 2, uavTable);
                    encoder.Dispatch((width + 7) / 8, (height + 7) / 8, 1);
                }),
            keepAlive);
        if (computeCompatible)
        {
            graph.DeclareComputeCompatible(pass);
        }
        graph.DeclareRecordingIndependent(pass);
    };
    const RGHandle aoInputs[]{m_inputs.depth, m_inputs.normal};
    const RGHandle aoOutputs[]{m_rawOutput};
    const RHIFormat aoFormats[]{kAOFormat};
    dispatch("SSAO.Compute", m_aoPSO, params, aoInputs, aoOutputs, aoFormats, false, true);

    const uint32_t readIndex = m_historyIndex;
    const uint32_t writeIndex = 1 - readIndex;
    ce::fixed_array<RGHandle, kHistoryCount> history{}, geometry{};
    constexpr const char* historyNames[]{"SSAO.History.Read", "SSAO.History.Write"};
    constexpr const char* geometryNames[]{"SSAO.GeometryHistory.Read", "SSAO.GeometryHistory.Write"};
    for (uint32_t role = 0; role < kHistoryCount; ++role)
    {
        const uint32_t i = role == 0 ? readIndex : writeIndex;
        history[i] = graph.ImportTexture(m_history[i], m_historyState[i], historyNames[role], &m_historyState[i]);
        geometry[i] =
            graph.ImportTexture(m_geometryHistory[i], m_geometryState[i], geometryNames[role], &m_geometryState[i]);
    }
    if (versioned)
    {
        history[writeIndex] = graph.Write(history[writeIndex]);
        geometry[writeIndex] = graph.Write(geometry[writeIndex]);
    }
    m_historyOutput = history[writeIndex];
    SSAOTemporalParams temporal{};
    const bool hasMotion =
        m_inputs.motionVectors.IsValid() && m_inputs.responsiveMask.IsValid() && m_inputs.reactiveMask.IsValid();
    if (context.camera)
    {
        temporal.inverseProjection = math::transpose(context.camera->inverseProjection);
        temporal.inverseView = math::transpose(context.camera->inverseView);
    }
    temporal.previousViewProjection = math::transpose(m_previousViewProjection);
    temporal.previousView = math::transpose(m_previousView);
    temporal.width = m_width;
    temporal.height = m_height;
    temporal.fullWidth = context.width;
    temporal.fullHeight = context.height;
    temporal.hasHistory = m_historyValid && context.camera && m_tuning.temporalEnabled;
    temporal.hasMotion = hasMotion;
    temporal.maxFrames = (std::max)(1u, (std::min)(m_tuning.maxHistoryFrames, 32u));
    temporal.depthTolerance = m_tuning.historyDepthTolerance;
    temporal.normalThreshold = m_tuning.historyNormalThreshold;
    temporal.jitterX = context.temporalFrame.previousJitterX - context.temporalFrame.jitterX;
    temporal.jitterY = context.temporalFrame.previousJitterY - context.temporalFrame.jitterY;
    const RGHandle temporalInputs[]{m_rawOutput,
                                    m_inputs.normal,
                                    m_inputs.depth,
                                    history[readIndex],
                                    geometry[readIndex],
                                    hasMotion ? m_inputs.motionVectors : m_inputs.normal,
                                    hasMotion ? m_inputs.responsiveMask : m_inputs.depth,
                                    hasMotion ? m_inputs.reactiveMask : m_inputs.depth};
    const RGHandle temporalOutputs[]{m_resolved, history[writeIndex], geometry[writeIndex]};
    const RHIFormat temporalFormats[]{kAOFormat, RHIFormat::RGBA16Float, RHIFormat::RGBA32Float};
    // Persistent history is ordered on graphics across submissions, including
    // frames whose transient AO/filter dispatches run on a compute queue.
    dispatch("SSAO.Temporal", m_temporalPSO, temporal, temporalInputs, temporalOutputs, temporalFormats, true, false);

    const auto generation = m_historyGeneration;
    const auto currentView = context.camera ? context.camera->view : math::matrix4x4::identity();
    const auto currentViewProjection =
        context.camera ? currentView * context.camera->projection : math::matrix4x4::identity();
    m_resourceRetirement.WatchRecording(
        [this, generation]
        {
            if (generation == m_historyGeneration)
            {
                ResetHistory();
            }
        },
        [this, generation, writeIndex, currentView, currentViewProjection, hasCamera = context.camera != nullptr]
        {
            if (generation == m_historyGeneration)
            {
                m_historyIndex = writeIndex;
                m_previousView = currentView;
                m_previousViewProjection = currentViewProjection;
                m_historyValid = hasCamera;
            }
        });
    const RGHandle filterInputs[]{m_resolved, m_inputs.normal};
    const RGHandle filterOutputs[]{m_output};
    dispatch("SSAO.Filter", m_filterPSO, params, filterInputs, filterOutputs, aoFormats, m_keepAlive, true);
}

void EnhancedSSAOPass::ReleaseHistory(const EnhancedFrameContext& context)
{
    for (uint32_t i = 0; i < kHistoryCount; ++i)
    {
        if (m_history[i].IsValid())
        {
            context.resources->ReleaseTexture(m_history[i]);
        }
        if (m_geometryHistory[i].IsValid())
        {
            context.resources->ReleaseTexture(m_geometryHistory[i]);
        }
    }
    m_history.fill({});
    m_geometryHistory.fill({});
    m_historyState.fill(RHIResourceState::ShaderResource);
    m_geometryState.fill(RHIResourceState::ShaderResource);
    m_resourceRetirement.ClearAfterIdle();
    ResetHistory();
}

void EnhancedSSAOPass::Shutdown()
{
    if (auto* device = m_resourceRetirement.Device())
    {
        EnhancedFrameContext context{};
        context.resources = device;
        ReleaseHistory(context);
    }
    m_width = m_height = m_frameIndex = 0;
    m_aoPSO = m_temporalPSO = m_filterPSO = {};
}
