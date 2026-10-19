#include "GpuGeometryOcclusion.h"
#include "../EngineDiagnostics/ProfileScope.h"

#include "RHI/RHIShaderSource.h"
#include "Render/Graph/EnhancedRenderPass.h"

#include "../Utility_Framework/StandardLibrary.h"

namespace
{
struct SharedGeometryOcclusionConstants
{
    std::uint32_t sourceWidth{}, sourceHeight{}, targetWidth{}, targetHeight{};
};
static_assert(sizeof(SharedGeometryOcclusionConstants) == 16);

bool SameOcclusionHandle(RGHandle left, RGHandle right)
{
    return left.index == right.index && left.version == right.version && left.kind == right.kind &&
           left.epoch == right.epoch;
}
} // namespace

struct GpuGeometryOcclusion::HistoryPool
{
    struct Entry
    {
        std::uint32_t width{}, height{};
        std::uint64_t bytes{};
        std::vector<RHITextureHandle> textures;
    };
    IRenderDeviceServices& device;
    std::mutex mutex;
    std::vector<Entry> free;
    std::uint64_t cachedBytes{};
    bool closed{};
    explicit HistoryPool(IRenderDeviceServices& value) : device(value) {}
    ~HistoryPool() { Close(); }
    static std::uint64_t Bytes(std::uint32_t width, std::uint32_t height)
    {
        std::uint64_t bytes{};
        do
        {
            width = (width + 1u) / 2u;
            height = (height + 1u) / 2u;
            bytes += std::uint64_t(width) * height * sizeof(float);
        } while (width > 1u || height > 1u);
        return bytes;
    }
    bool Acquire(std::uint32_t width, std::uint32_t height, std::vector<RHITextureHandle>& textures)
    {
        std::lock_guard lock(mutex);
        const auto found = std::ranges::find_if(
            free, [&](const auto& entry) { return entry.width == width && entry.height == height; });
        if (closed || found == free.end())
        {
            return false;
        }
        textures = std::move(found->textures);
        cachedBytes -= found->bytes;
        free.erase(found);
        return true;
    }
    void Return(std::uint32_t width, std::uint32_t height, std::vector<RHITextureHandle> textures, bool reusable)
    {
        std::lock_guard lock(mutex);
        const auto bytes = Bytes(width, height);
        constexpr std::uint64_t budget = 128ull << 20;
        if (!closed && reusable && free.size() < 32 && bytes <= budget - cachedBytes)
        {
            free.push_back({width, height, bytes, std::move(textures)});
            cachedBytes += bytes;
        }
        else
        {
            for (const auto texture : textures)
            {
                device.ReleaseTexture(texture);
            }
        }
    }
    void Close()
    {
        std::lock_guard lock(mutex);
        closed = true;
        for (const auto& entry : free)
        {
            for (const auto texture : entry.textures)
            {
                device.ReleaseTexture(texture);
            }
        }
        free.clear();
        cachedBytes = 0;
    }
};

GpuGeometryOcclusion::ViewKey GpuGeometryOcclusion::CaptureView(const EnhancedFrameContext& context,
                                                                const math::matrix4x4& viewProjection)
{
    ViewKey key;
    key.device = context.resources;
    key.recording = context.resources ? context.resources->GetCurrentUploadRecordingId() : 0u;
    key.descriptors = context.resources ? context.resources->GetDescriptorVersionToken() : 0u;
    key.frameId = context.frameId;
    key.sceneEpoch = context.sceneEpoch;
    key.width = context.width;
    key.height = context.height;
    for (unsigned row = 0; row < 4; ++row)
    {
        for (unsigned column = 0; column < 4; ++column)
        {
            key.projectionBits[row * 4u + column] = std::bit_cast<std::uint32_t>(viewProjection.m[row][column]);
        }
    }
    return key;
}

std::uint32_t GpuGeometryOcclusion::LevelCount(std::uint32_t width, std::uint32_t height)
{
    if (!width || !height || width > (1u << kMaximumLevels) || height > (1u << kMaximumLevels))
    {
        return 0;
    }
    std::uint32_t levels = 0;
    do
    {
        width = (width + 1u) / 2u;
        height = (height + 1u) / 2u;
        ++levels;
    } while (width > 1u || height > 1u);
    return levels;
}

struct GpuGeometryOcclusion::DeclarationLayout
{
    std::uint32_t width{}, height{};
    std::string name, buildName;
    std::array<RGTextureDesc, kMaximumLevels> textures;
    std::array<std::string, kMaximumLevels> nextHistoryNames;
};

bool GpuGeometryOcclusion::Prepare(const EnhancedFrameContext& context, const math::matrix4x4& viewProjection,
                                   std::shared_ptr<const Pyramid>& result, std::string& error, const std::string& name)
{
    result.reset();
    const auto levels = LevelCount(context.width, context.height);
    if (!context.resources || !context.rootSignatures || !context.psoManager || !levels ||
        !context.resources->GetCurrentUploadRecordingId() || (m_device && m_device != context.resources))
    {
        error = "Current-depth occlusion requires one live device/recording and a supported nonempty view.";
        return false;
    }
    if (!m_build.GetGeneration())
    {
        const auto source = RHIShaderSource::Resolve("GeometryOcclusionBuild.slang").string();
        RHIShaderCompileOptions options;
        options.strictMath = true;
        LX::Runtime::CompiledCompute compiled;
        if (!LX::Runtime::CompileCompute(source, "GeometryOcclusionBuildCS", {}, options, compiled, error))
        {
            return false;
        }
        const RHIPipelineLayoutParam parameters[]{RHILayout::Cbv(0), RHILayout::SrvTable(1, 0),
                                                  RHILayout::UavTable(1, 0)};
        const auto layout = context.rootSignatures->GetOrCreate({parameters, {}}, error);
        if (!layout.IsValid())
        {
            return false;
        }
        RHIComputePipelineDesc description;
        description.layout = layout;
        description.csBytecode = compiled.stage.bytecode.Data();
        description.csSize = compiled.stage.bytecode.Size();
        if (!m_build.Create(*context.psoManager, description, std::move(compiled.description), error))
        {
            return false;
        }
        m_device = context.resources;
    }
    std::shared_ptr<const DeclarationLayout> declarationLayout;
    for (const auto& candidate : std::span{m_declarationLayouts})
    {
        if (candidate && candidate->width == context.width && candidate->height == context.height &&
            candidate->name == name)
        {
            declarationLayout = candidate;
            break;
        }
    }
    if (!declarationLayout)
    {
        ce::profile_scope layoutProfile{ce::marker<"GeometryOcclusionLayoutBuild">()};
        auto layout = std::make_shared<DeclarationLayout>();
        layout->width = context.width;
        layout->height = context.height;
        layout->name = name;
        layout->buildName = name + ".Build";
        const auto historyName = name == "Geometry.Occlusion" ? std::string("METEOR.Camera") : name;
        auto width = context.width, height = context.height;
        for (std::uint32_t level = 0; level < levels; ++level)
        {
            width = (width + 1u) / 2u;
            height = (height + 1u) / 2u;
            auto& description = layout->textures[level];
            description.width = width;
            description.height = height;
            description.format = RHIFormat::R32Float;
            description.allowUnorderedAccess = true;
            const auto suffix = std::to_string(level);
            description.name = name + "." + suffix;
            layout->nextHistoryNames[level] = historyName + ".NextHZB." + suffix;
        }
        // Only dimensions/names/descriptions are retained. Handles, versions,
        // recording IDs, projection, barriers and dispatch data stay per frame.
        m_declarationLayouts[m_nextDeclarationLayout] = layout;
        m_nextDeclarationLayout = (m_nextDeclarationLayout + 1u) % m_declarationLayouts.size();
        declarationLayout = std::move(layout);
    }
    else
    {
        ce::profile_scope layoutProfile{ce::marker<"GeometryOcclusionLayoutReuse">()};
    }
    auto pyramid = std::make_shared<Pyramid>();
    pyramid->m_declarationLayout = std::move(declarationLayout);
    pyramid->m_view = CaptureView(context, viewProjection);
    pyramid->m_name = name;
    pyramid->m_levelCount = levels;
    pyramid->m_build = m_build.GetGeneration();
    pyramid->m_self = pyramid;
    result = std::move(pyramid);
    error.clear();
    return true;
}

void GpuGeometryOcclusion::Pyramid::CheckCurrent(const EnhancedRenderGraph* graph) const
{
    if (!m_view.device || m_view.device->GetCurrentUploadRecordingId() != m_view.recording ||
        m_view.device->GetDescriptorVersionToken() != m_view.descriptors ||
        (graph &&
         (m_graph != graph || m_graphEpoch != graph->ResourceEpoch() || &graph->DeviceServices() != m_view.device)))
    {
        throw std::runtime_error("Current-depth pyramid escaped its device, recording, descriptors or graph epoch.");
    }
}

GpuGeometryOcclusion::History::~History()
{
    if (pool)
    {
        // Submitted frames retain their histories through GPU completion. A
        // recorded but uncommitted/aborted history has no known final state.
        pool->Return(source.width, source.height, std::move(textures), recorded.load() && completion != 0);
        return;
    }
    if (device)
        for (auto texture : textures)
            device->ReleaseTexture(texture);
}

bool GpuGeometryOcclusion::PrepareHistory(const EnhancedFrameContext& context, const math::matrix4x4& projection,
                                          std::uint64_t viewId, std::uint64_t revision,
                                          std::shared_ptr<const Pyramid>& result, std::string& error,
                                          const std::string& name)
{
    ce::profile_scope profile{ce::marker<"GeometryHistoryPrepare">()};
    if (!Prepare(context, projection, result, error, name))
        return false;
    auto pyramid = std::const_pointer_cast<Pyramid>(result);
    auto history = std::make_shared<History>();
    history->device = context.resources;
    history->source = pyramid->m_view;
    history->viewId = viewId;
    history->revision = revision;
    history->name = name == "Geometry.Occlusion" ? "METEOR.Camera" : name;
    if (!m_historyPool)
    {
        m_historyPool = std::make_shared<HistoryPool>(*context.resources);
    }
    history->pool = m_historyPool;
    if (m_historyPool->Acquire(context.width, context.height, history->textures))
    {
        history->initialState = RHIResourceState::ShaderResource;
        pyramid->m_history = std::move(history);
        return true;
    }
    ce::profile_scope allocationProfile{ce::marker<"GeometryHistoryCreate">()};
    auto width = context.width, height = context.height;
    for (unsigned level = 0; level < pyramid->m_levelCount; ++level)
    {
        width = (width + 1u) / 2u;
        height = (height + 1u) / 2u;
        RHITextureDesc desc;
        desc.width = width;
        desc.height = height;
        desc.format = RHIFormat::R32Float;
        desc.allowUnorderedAccess = true;
        const auto debugName = history->name + ".HistoryHZB";
        const std::wstring wideName(debugName.begin(), debugName.end());
        desc.debugName = wideName.c_str();
        RHITextureHandle texture;
        if (!context.resources->CreateTexture(desc, texture, error))
        {
            result.reset();
            return false;
        }
        history->textures.push_back(texture);
    }
    pyramid->m_history = std::move(history);
    return true;
}

std::shared_ptr<const GpuGeometryOcclusion::Pyramid> GpuGeometryOcclusion::ReadHistory(
    const EnhancedFrameContext& context, const math::matrix4x4& projection, const std::shared_ptr<History>& history)
{
    if (!history || !history->recorded.load() || !history->completion || history->device != context.resources ||
        history->source.width != context.width || history->source.height != context.height ||
        history->source.sceneEpoch != context.sceneEpoch || history->source.frameId >= context.frameId)
        throw std::runtime_error("History HZB does not match the current device, extent, scene or frame order.");
    auto pyramid = std::make_shared<Pyramid>();
    pyramid->m_view = CaptureView(context, projection);
    pyramid->m_levelCount = static_cast<std::uint32_t>(history->textures.size());
    pyramid->m_history = history;
    pyramid->m_historyReader = true;
    pyramid->m_name = history->name;
    pyramid->m_self = pyramid;
    return pyramid;
}

void GpuGeometryOcclusion::Pyramid::ImportHistory(EnhancedRenderGraph& graph) const
{
    CheckCurrent();
    if (!m_historyReader || !m_history || graph.GetSchedulingMode() != RGSchedulingMode::ExplicitVersioned ||
        &graph.DeviceServices() != m_view.device)
        throw std::runtime_error("History HZB import requires its recording-local RG2 reader.");
    if (m_graph)
    {
        CheckCurrent(&graph);
        return;
    }
    m_graph = &graph;
    m_graphEpoch = graph.ResourceEpoch();
    for (auto texture : m_history->textures)
    {
        auto handle = graph.ImportTexture(texture, RHIResourceState::ShaderResource, m_history->name + ".PreviousHZB");
        graph.RequireImportedFinalState(handle, RHIResourceState::ShaderResource);
        m_levels.push_back(handle);
    }
}

void GpuGeometryOcclusion::Pyramid::RequireView(const ViewKey& view) const
{
    CheckCurrent();
    if (view != m_view)
    {
        throw std::runtime_error("Current-depth pyramid and culler do not describe the same sealed view.");
    }
}

void GpuGeometryOcclusion::Pyramid::RequireCurrentView(const ViewKey& view) const
{
    RequireView(view);
    if (m_historyReader)
    {
        throw std::runtime_error("Camera depth prediction requires a current-depth producer, not retained history.");
    }
}

void GpuGeometryOcclusion::Pyramid::Declare(EnhancedRenderGraph& graph, RGHandle depth) const
{
    CheckCurrent();
    if (m_historyReader)
        throw std::runtime_error("An immutable history HZB cannot be overwritten.");
    if (m_graph)
    {
        CheckCurrent(&graph);
        if (!SameOcclusionHandle(depth, m_depth))
        {
            throw std::runtime_error("Current-depth pyramid was redeclared with a different depth version.");
        }
        return;
    }
    if (graph.GetSchedulingMode() != RGSchedulingMode::ExplicitVersioned || &graph.DeviceServices() != m_view.device ||
        !depth.IsValid() || depth.kind != RGResourceKind::Texture || depth.epoch != graph.ResourceEpoch())
    {
        throw std::runtime_error("Current-depth pyramid requires an earlier same-view RG2 depth version.");
    }
    const auto owner = m_self.lock();
    if (!owner)
    {
        throw std::runtime_error("Current-depth pyramid requires its retained prepared owner.");
    }
    m_graph = &graph;
    m_graphEpoch = graph.ResourceEpoch();
    m_depth = depth;
    std::vector<EnhancedRenderGraph::RGPassUsage> uses;
    std::vector<EnhancedRenderGraph::RepeatedPhase> phases;
    uses.reserve(m_levelCount + 1u);
    uses.push_back({depth, RHIResourceState::ShaderResource, RGAccessMode::Read});
    phases.reserve(m_levelCount);
    m_levels.reserve(m_levelCount);
    auto source = depth;
    for (std::uint32_t level = 0; level < m_levelCount; ++level)
    {
        const auto& description = m_declarationLayout->textures[level];
        const auto resource = m_history ? graph.ImportTexture(m_history->textures.at(level), m_history->initialState,
                                                              m_declarationLayout->nextHistoryNames[level])
                                        : graph.CreateTexture(description);
        if (m_history)
        {
            graph.RequireImportedFinalState(resource, RHIResourceState::ShaderResource);
        }
        const auto target = graph.Write(resource);
        m_levels.push_back(target);
        uses.push_back({target, RHIResourceState::UnorderedAccess, RGAccessMode::Write});
        auto& phase = phases.emplace_back();
        phase.name = description.name;
        phase.usages.reserve(2);
        phase.usages.push_back({source, RHIResourceState::ShaderResource, RGAccessMode::Read});
        phase.usages.push_back({target, RHIResourceState::UnorderedAccess, RGAccessMode::Write});
        source = target;
    }
    // Mips are a serial dependency chain. Keep their resource versions and
    // per-phase transitions explicit without allocating a complete Pass per mip.
    const auto buildPass = graph.AddRepeatedPass(m_declarationLayout->buildName, std::move(uses), std::move(phases), 1,
        [owner, depth](const auto& execution, std::uint32_t, std::uint32_t level) {
            const auto divisor = 1u << level;
            const auto phaseWidth = (owner->m_view.width + divisor - 1u) / divisor;
            const auto phaseHeight = (owner->m_view.height + divisor - 1u) / divisor;
            const SharedGeometryOcclusionConstants constants{
                phaseWidth, phaseHeight, (phaseWidth + 1u) / 2u, (phaseHeight + 1u) / 2u};
            const auto phaseSource = level ? owner->m_levels[level - 1u] : depth;
            const auto phaseTarget = owner->m_levels[level];
            owner->CheckCurrent(execution.graph);
            auto* device = owner->m_view.device;
            const auto sourceTexture = execution.ResolveHandle(phaseSource);
            const auto info = device->DescribeTexture(sourceTexture);
            if (info.width != constants.sourceWidth || info.height != constants.sourceHeight ||
                info.depthOrArraySize != 1u ||
                info.format != (level == 0u ? RHIFormat::D32Float : RHIFormat::R32Float))
            {
                throw std::runtime_error("Current-depth pyramid source does not match its prepared view.");
            }
            const RHIBindingDesc input[]{level == 0u ? RHIBindingDesc::SrvDepth(sourceTexture)
                                                     : RHIBindingDesc::Srv2D(sourceTexture, RHIFormat::R32Float)};
            const RHIBindingDesc output[]{
                RHIBindingDesc::Uav2D(execution.ResolveHandle(phaseTarget), RHIFormat::R32Float)};
            const auto inputs = device->CreateBindings(input);
            const auto outputs = device->CreateBindings(output);
            const auto uploaded = device->UploadConstants(&constants, sizeof(constants));
            if (!inputs.IsValid() || !outputs.IsValid() || !uploaded.IsValid())
            {
                throw std::runtime_error("Current-depth pyramid binding or upload failed.");
            }
            auto& encoder = *execution.encoder;
            encoder.SetPipeline(RHIBindPoint::Compute, owner->m_build->GetHandle());
            encoder.SetConstantBuffer(RHIBindPoint::Compute, 0, uploaded);
            encoder.SetBindings(RHIBindPoint::Compute, 1, inputs);
            encoder.SetBindings(RHIBindPoint::Compute, 2, outputs);
            encoder.Dispatch((constants.targetWidth + 7u) / 8u, (constants.targetHeight + 7u) / 8u, 1u);
            if (owner->m_history && level + 1u == owner->m_levelCount)
            {
                owner->m_history->recorded.store(true);
            }
        }, bool(m_history), m_levelCount);
    graph.DeclareComputeCompatible(buildPass);
    graph.DeclareRecordingIndependent(buildPass);
}

void GpuGeometryOcclusion::Pyramid::AddReadUsages(EnhancedRenderGraph& graph,
                                                  std::vector<EnhancedRenderGraph::RGPassUsage>& usages) const
{
    CheckCurrent(&graph);
    if (m_levels.size() != m_levelCount)
    {
        throw std::runtime_error("Current-depth pyramid is not completely declared.");
    }
    for (const auto level : m_levels)
    {
        const auto duplicate = std::find_if(
            std::span{usages}.begin(), std::span{usages}.end(), [&](const auto& usage) { return SameOcclusionHandle(level, usage.handle); });
        if (duplicate == std::span{usages}.end())
        {
            usages.push_back({level, RHIResourceState::ShaderResource, RGAccessMode::Read});
        }
        else if (duplicate->state != RHIResourceState::ShaderResource || duplicate->access != RGAccessMode::Read)
        {
            throw std::runtime_error("Current-depth pyramid consumer has conflicting resource usage.");
        }
    }
}

std::array<RHIBindingDesc, GpuGeometryOcclusion::kMaximumLevels> GpuGeometryOcclusion::Pyramid::BindingInputs(
    const EnhancedRenderGraph::ExecuteContext& execution) const
{
    CheckCurrent(execution.graph);
    if (m_levels.size() != m_levelCount || m_levels.empty())
    {
        throw std::runtime_error("Current-depth pyramid binding requires a complete declaration.");
    }
    std::array<RHIBindingDesc, kMaximumLevels> inputs;
    for (std::size_t index = 0; index < inputs.size(); ++index)
    {
        const auto levelIndex = (std::min)(index, m_levels.size() - 1u);
        inputs[index] = RHIBindingDesc::Srv2D(execution.ResolveHandle(m_levels[levelIndex]), RHIFormat::R32Float);
    }
    return inputs;
}

RHIBindingTable GpuGeometryOcclusion::Pyramid::Bindings(const EnhancedRenderGraph::ExecuteContext& execution) const
{
    const auto inputs = BindingInputs(execution);
    const auto bindings = m_view.device->CreateBindings(inputs);
    if (!bindings.IsValid())
    {
        throw std::runtime_error("Current-depth pyramid consumer bindings failed.");
    }
    return bindings;
}

void GpuGeometryOcclusion::ShutdownAfterIdle()
{
    if (m_historyPool)
    {
        m_historyPool->Close();
        m_historyPool.reset();
    }
    m_declarationLayouts.fill({});
    m_nextDeclarationLayout = 0;
    m_build = {};
    m_device = nullptr;
}
