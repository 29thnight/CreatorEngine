// The runner supplies exact production HZB classes, declarations and callbacks.
// This service fixture records CPU contracts; it does not execute a GPU shader.
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iostream>
#include <memory>
#include <new>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace
{
    bool count_allocations{};
    size_t allocation_count{}, checks{};
    void require(bool condition, const char* message)
    {
        ++checks;
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }
    template<class Function>
    bool rejects(Function&& function)
    {
        try
        {
            function();
        }
        catch (const std::exception&)
        {
            return true;
        }
        return false;
    }
} // namespace
void* operator new(size_t bytes)
{
    allocation_count += count_allocations;
    if (auto* result = std::malloc(bytes ? bytes : 1))
    {
        return result;
    }
    throw std::bad_alloc();
}
void* operator new[](size_t bytes)
{
    return ::operator new(bytes);
}
void operator delete(void* value) noexcept
{
    std::free(value);
}
void operator delete[](void* value) noexcept
{
    std::free(value);
}
void operator delete(void* value, size_t) noexcept
{
    std::free(value);
}
void operator delete[](void* value, size_t) noexcept
{
    std::free(value);
}

namespace math
{
    struct matrix4x4
    {
        float m[4][4]{};
    };
} // namespace math
enum class RHIFormat
{
    D32Float,
    R32Float
};
enum class RHIResourceState
{
    Common,
    ShaderResource,
    UnorderedAccess
};
enum class RGResourceKind
{
    Texture,
    Buffer
};
enum class RGAccessMode
{
    Read,
    Write,
    ReadWrite
};
enum class RGSchedulingMode
{
    DeclarationOrder,
    ExplicitVersioned
};
struct RGHandle
{
    uint16_t index{0xffff}, version{};
    RGResourceKind kind{RGResourceKind::Texture};
    uint64_t epoch{};
    bool IsValid() const { return index != 0xffff; }
    bool operator==(const RGHandle&) const = default;
};
struct RHITextureHandle
{
    uint32_t id{};
    bool operator==(const RHITextureHandle&) const = default;
};
struct RGTextureDesc
{
    uint32_t width{}, height{};
    RHIFormat format{RHIFormat::R32Float};
    bool allowUnorderedAccess{};
    std::string name;
};
struct RHIBindingDesc
{
    uint32_t kind{}, resource{}, format{}, depth{};
    static RHIBindingDesc SrvDepth(RHITextureHandle texture) { return {1, texture.id, 0, 1}; }
    static RHIBindingDesc Srv2D(RHITextureHandle texture, RHIFormat format)
    {
        return {1, texture.id, static_cast<uint32_t>(format), 0};
    }
    static RHIBindingDesc Uav2D(RHITextureHandle texture, RHIFormat format)
    {
        return {2, texture.id, static_cast<uint32_t>(format), 0};
    }
    bool operator==(const RHIBindingDesc&) const = default;
};
struct RHIBindingTable
{
    uint32_t id{};
    bool IsValid() const { return id != 0; }
};
struct RHIBufferSlice
{
    uint32_t id{};
    bool IsValid() const { return id != 0; }
};
enum class RHIBindPoint
{
    Compute
};
namespace LX::Runtime
{
    struct ComputeGeneration
    {
        uint32_t GetHandle() const { return 9; }
    };
    struct ComputePipeline
    {
    };
} // namespace LX::Runtime
struct Event
{
    std::array<char, 128> m_name{};
    std::array<uint64_t, 8> m_values{};
    bool operator==(const Event&) const = default;
};
class Trace
{
  public:
    std::vector<Event> m_events;
    Trace() { m_events.reserve(512); }
    template<class... Values>
    void Add(std::string_view name, Values... values)
    {
        require(name.size() < Event{}.m_name.size(), "Trace name capacity");
        m_events.emplace_back();
        auto& event = m_events.back();
        std::copy(name.begin(), name.end(), event.m_name.begin());
        event.m_values = {static_cast<uint64_t>(values)...};
    }
};
class IRenderDeviceServices
{
  public:
    struct Description
    {
        uint32_t width{}, height{}, depthOrArraySize{1};
        RHIFormat format{RHIFormat::R32Float};
    };
    Trace& m_trace;
    std::array<Description, 256> m_descriptions;
    uint64_t m_recording{31}, m_descriptors{37};
    uint32_t m_bindingCount{}, m_uploadCount{}, m_failBinding{}, m_failUpload{};
    explicit IRenderDeviceServices(Trace& trace) : m_trace(trace) {}
    uint64_t GetCurrentUploadRecordingId() const { return m_recording; }
    uint64_t GetDescriptorVersionToken() const { return m_descriptors; }
    Description DescribeTexture(RHITextureHandle texture)
    {
        m_trace.Add("DescribeTexture", texture.id);
        return m_descriptions.at(texture.id);
    }
    RHIBindingTable CreateBindings(std::span<const RHIBindingDesc> values)
    {
        const auto serial = ++m_bindingCount;
        m_trace.Add("CreateBindings", serial, values.size());
        for (const auto& value : values)
        {
            m_trace.Add("Binding", value.kind, value.resource, value.format, value.depth);
        }
        return {serial == m_failBinding ? 0u : serial};
    }
    RHIBufferSlice UploadConstants(const void* data, size_t bytes)
    {
        require(bytes == 4 * sizeof(uint32_t), "HZB constant ABI");
        std::array<uint32_t, 4> values;
        std::memcpy(values.data(), data, bytes);
        const auto serial = ++m_uploadCount;
        m_trace.Add("UploadConstants", serial, values[0], values[1], values[2], values[3]);
        return {serial == m_failUpload ? 0u : serial};
    }
};
class RHIEncoder
{
  public:
    Trace& m_trace;
    explicit RHIEncoder(Trace& trace) : m_trace(trace) {}
    void SetPipeline(RHIBindPoint point, uint32_t pipeline) { m_trace.Add("SetPipeline", point, pipeline); }
    void SetConstantBuffer(RHIBindPoint point, uint32_t root, RHIBufferSlice buffer)
    {
        m_trace.Add("SetConstantBuffer", point, root, buffer.id);
    }
    void SetBindings(RHIBindPoint point, uint32_t root, RHIBindingTable table)
    {
        m_trace.Add("SetBindings", point, root, table.id);
    }
    void Dispatch(uint32_t x, uint32_t y, uint32_t z) { m_trace.Add("Dispatch", x, y, z); }
};
class EnhancedRenderGraph
{
  public:
    struct RGPassUsage
    {
        RGHandle handle;
        RHIResourceState state{};
        RGAccessMode access{};
        bool operator==(const RGPassUsage&) const = default;
    };
    struct RepeatedPhaseView
    {
        std::string_view name;
        std::span<const RGPassUsage> usages;
    };
    struct ExecuteContext
    {
        const EnhancedRenderGraph* graph{};
        RHIEncoder* encoder{};
        RHITextureHandle ResolveHandle(RGHandle handle) const { return graph->ResolveHandle(handle); }
    };
    Trace& m_trace;
    IRenderDeviceServices& m_device;
    RGSchedulingMode m_mode{RGSchedulingMode::ExplicitVersioned};
    uint64_t m_epoch{43};
    std::array<RHITextureHandle, 64> m_resources;
    std::array<uint16_t, 64> m_versions;
    uint32_t m_count{1}, m_importCount{}, m_failImport{}, m_phaseCount{};
    std::function<void(const ExecuteContext&, uint32_t, uint32_t)> m_execute;
    explicit EnhancedRenderGraph(Trace& trace, IRenderDeviceServices& device) : m_trace(trace), m_device(device)
    {
        m_resources[0] = {1};
        m_versions.fill(0);
    }
    uint64_t ResourceEpoch() const { return m_epoch; }
    IRenderDeviceServices& DeviceServices() const { return m_device; }
    RGSchedulingMode GetSchedulingMode() const { return m_mode; }
    RGHandle CreateTexture(const RGTextureDesc& desc)
    {
        const auto index = static_cast<uint16_t>(m_count++);
        m_resources[index] = {uint32_t(index) + 100};
        m_device.m_descriptions[m_resources[index].id] = {desc.width, desc.height, 1, desc.format};
        m_trace.Add(desc.name, index, desc.width, desc.height, desc.format, desc.allowUnorderedAccess);
        return {index, 0, RGResourceKind::Texture, m_epoch};
    }
    RGHandle ImportTexture(RHITextureHandle texture, RHIResourceState state, std::string_view name)
    {
        const auto ordinal = ++m_importCount;
        if (ordinal == m_failImport)
        {
            m_trace.Add("ImportFailure", ordinal);
            throw std::runtime_error("Injected import failure");
        }
        const auto index = static_cast<uint16_t>(m_count++);
        m_resources[index] = texture;
        m_trace.Add(name, ordinal, index, texture.id, state);
        return {index, 0, RGResourceKind::Texture, m_epoch};
    }
    void RequireImportedFinalState(RGHandle handle, RHIResourceState state)
    {
        m_trace.Add("RequireImportedFinalState", handle.index, handle.version, state);
    }
    RGHandle Write(RGHandle handle)
    {
        handle.version = ++m_versions[handle.index];
        m_trace.Add("Write", handle.index, handle.version);
        return handle;
    }
    void LogUsage(const RGPassUsage& usage)
    {
        m_trace.Add("Usage", usage.handle.index, usage.handle.version, usage.handle.kind, usage.handle.epoch,
                    usage.state, usage.access);
    }
    uint32_t AddRepeatedPassView(std::string_view name, std::span<const RGPassUsage> uses,
                                 std::span<const RepeatedPhaseView> phases, uint32_t repeat,
                                 std::function<void(const ExecuteContext&, uint32_t, uint32_t)> execute,
                                 bool sideEffect, uint32_t work)
    {
        m_trace.Add(name, uses.size(), phases.size(), repeat, sideEffect, work);
        for (const auto& usage : uses)
        {
            LogUsage(usage);
        }
        for (const auto& phase : phases)
        {
            m_trace.Add(phase.name, phase.usages.size());
            for (const auto& usage : phase.usages)
            {
                LogUsage(usage);
            }
        }
        m_phaseCount = static_cast<uint32_t>(phases.size());
        m_execute = std::move(execute);
        return 0;
    }
    void DeclareComputeCompatible(uint32_t pass) { m_trace.Add("ComputeCompatible", pass); }
    void DeclareRecordingIndependent(uint32_t pass) { m_trace.Add("RecordingIndependent", pass); }
    RHITextureHandle ResolveHandle(RGHandle handle) const
    {
        require(handle.IsValid() && handle.index < m_count, "Invalid HZB graph handle");
        m_trace.Add("ResolveHandle", handle.index, handle.version, handle.epoch);
        return m_resources[handle.index];
    }
    void Execute(RHIEncoder& encoder)
    {
        for (uint32_t phase = 0; phase < m_phaseCount; ++phase)
        {
            m_execute({this, &encoder}, 0, phase);
        }
    }
};
struct EnhancedFrameContext
{
    IRenderDeviceServices* resources{};
    uint32_t width{}, height{};
    uint64_t frameId{101}, sceneEpoch{103};
};

#define private public
namespace baseline
{
#include "BaselineHzb.inl"
    GpuGeometryOcclusion::History::~History() = default;
} // namespace baseline
namespace current
{
#include "CurrentHzb.inl"
    GpuGeometryOcclusion::History::~History() = default;
} // namespace current
#undef private

namespace
{
    struct Services
    {
        Trace m_trace;
        IRenderDeviceServices m_device{m_trace};
        EnhancedRenderGraph m_graph{m_trace, m_device};
        RHIEncoder m_encoder{m_trace};
        EnhancedFrameContext m_context{&m_device, 0, 0};
        math::matrix4x4 m_projection;
        explicit Services(unsigned levels)
        {
            m_context.width = 1u << levels;
            m_context.height = levels > 1 ? 3u : 1u;
            m_device.m_descriptions[1] = {m_context.width, m_context.height, 1, RHIFormat::D32Float};
            for (unsigned row = 0; row < 4; ++row)
            {
                for (unsigned column = 0; column < 4; ++column)
                {
                    m_projection.m[row][column] = float(row * 4 + column) * .125f;
                }
            }
        }
        RGHandle Depth() const { return {0, 1, RGResourceKind::Texture, m_graph.m_epoch}; }
    };

    template<class Visibility>
    std::shared_ptr<typename Visibility::History> make_history(Services& services, unsigned levels)
    {
        auto history = std::make_shared<typename Visibility::History>();
        history->device = &services.m_device;
        history->source = Visibility::CaptureView(services.m_context, services.m_projection);
        --history->source.frameId;
        history->completion = 109;
        history->recorded = true;
        history->name = "METEOR.Shadow.History.Cascade.AllocationProbe";
        auto width = services.m_context.width, height = services.m_context.height;
        for (unsigned level = 0; level < levels; ++level)
        {
            width = (width + 1) / 2;
            height = (height + 1) / 2;
            history->textures.push_back({level + 20});
            services.m_device.m_descriptions[level + 20] = {width, height, 1, RHIFormat::R32Float};
        }
        return history;
    }

    template<class Visibility>
    std::shared_ptr<typename Visibility::Pyramid> make_writer(Services& services, unsigned levels, bool history)
    {
        auto pyramid = std::make_shared<typename Visibility::Pyramid>();
        using Layout = std::remove_const_t<typename decltype(pyramid->m_declarationLayout)::element_type>;
        auto layout = std::make_shared<Layout>();
        layout->name = "HZB.Probe";
        layout->buildName = "HZB.Probe.Build";
        auto width = services.m_context.width, height = services.m_context.height;
        for (unsigned level = 0; level < levels; ++level)
        {
            width = (width + 1) / 2;
            height = (height + 1) / 2;
            auto& desc = layout->textures[level];
            desc.width = width;
            desc.height = height;
            desc.format = RHIFormat::R32Float;
            desc.allowUnorderedAccess = true;
            desc.name = "HZB.Probe." + std::to_string(level);
            layout->nextHistoryNames[level] = "HZB.Next." + std::to_string(level);
        }
        pyramid->m_view = Visibility::CaptureView(services.m_context, services.m_projection);
        pyramid->m_declarationLayout = layout;
        pyramid->m_levelCount = levels;
        pyramid->m_build = std::make_shared<LX::Runtime::ComputeGeneration>();
        pyramid->m_self = pyramid;
        if (history)
        {
            pyramid->m_history = make_history<Visibility>(services, levels);
            pyramid->m_history->recorded = false;
        }
        return pyramid;
    }

    template<class Pyramid>
    void exercise_reads(const std::shared_ptr<Pyramid>& pyramid, Services& services, unsigned levels)
    {
        using Usage = EnhancedRenderGraph::RGPassUsage;
        std::vector<Usage> uses;
        pyramid->AddReadUsages(services.m_graph, uses);
        require(uses.size() == levels, "HZB active level count differs");
        const auto original = uses;
        pyramid->AddReadUsages(services.m_graph, uses);
        require(uses == original, "HZB read de-duplication differs");
        const EnhancedRenderGraph::ExecuteContext execution{&services.m_graph, &services.m_encoder};
        const auto bindings = pyramid->BindingInputs(execution);
        require(bindings.size() == 16, "HZB fixed descriptor count differs");
        for (size_t index = 0; index < bindings.size(); ++index)
        {
            const auto expected =
                services.m_graph.m_resources[uses[(std::min)(index, size_t(levels - 1))].handle.index];
            require(bindings[index] == RHIBindingDesc::Srv2D(expected, RHIFormat::R32Float),
                    "HZB descriptor tail does not repeat final mip");
        }
        require(pyramid->Bindings(execution).IsValid(), "HZB bindings unavailable");
        uses[0].access = RGAccessMode::Write;
        require(rejects([&] { pyramid->AddReadUsages(services.m_graph, uses); }), "Conflicting HZB write accepted");
        uses = original;
        uses[0].state = RHIResourceState::UnorderedAccess;
        require(rejects([&] { pyramid->AddReadUsages(services.m_graph, uses); }), "Conflicting HZB state accepted");
        ++services.m_graph.m_epoch;
        require(rejects([&] { pyramid->BindingInputs(execution); }), "Stale HZB graph epoch accepted");
        --services.m_graph.m_epoch;
        ++services.m_device.m_recording;
        require(rejects([&] { pyramid->AddReadUsages(services.m_graph, uses); }), "Stale upload recording accepted");
        --services.m_device.m_recording;
        ++services.m_device.m_descriptors;
        require(rejects([&] { pyramid->BindingInputs(execution); }), "Stale descriptor recording accepted");
        --services.m_device.m_descriptors;
    }

    void compare_history(unsigned levels, unsigned failure)
    {
        Services before(levels), after(levels);
        const auto oldHistory = make_history<baseline::GpuGeometryOcclusion>(before, levels);
        const auto newHistory = make_history<current::GpuGeometryOcclusion>(after, levels);
        const auto oldPyramid =
            baseline::GpuGeometryOcclusion::ReadHistory(before.m_context, before.m_projection, oldHistory);
        const auto newPyramid =
            current::GpuGeometryOcclusion::ReadHistory(after.m_context, after.m_projection, newHistory);
        before.m_graph.m_failImport = after.m_graph.m_failImport = failure;
        const auto oldFailure = rejects([&] { oldPyramid->ImportHistory(before.m_graph); });
        const auto newFailure = rejects([&] { newPyramid->ImportHistory(after.m_graph); });
        require(oldFailure == newFailure && oldFailure == bool(failure), "History import failure differs");
        require(before.m_trace.m_events == after.m_trace.m_events, "History import names/final-state order differ");
        const auto oldCount = before.m_trace.m_events.size(), newCount = after.m_trace.m_events.size();
        oldPyramid->ImportHistory(before.m_graph);
        newPyramid->ImportHistory(after.m_graph);
        require(before.m_trace.m_events.size() == oldCount && after.m_trace.m_events.size() == newCount,
                "Idempotent history import issued more work");
        if (failure)
        {
            std::vector<EnhancedRenderGraph::RGPassUsage> uses;
            require(rejects([&] { oldPyramid->AddReadUsages(before.m_graph, uses); }) &&
                        rejects([&] { newPyramid->AddReadUsages(after.m_graph, uses); }),
                    "Partial history became a complete producer");
            require(rejects([&] { oldPyramid->BindingInputs({&before.m_graph, &before.m_encoder}); }) &&
                        rejects([&] { newPyramid->BindingInputs({&after.m_graph, &after.m_encoder}); }),
                    "Partial history exposed descriptor inputs");
            return;
        }
        exercise_reads(oldPyramid, before, levels);
        exercise_reads(newPyramid, after, levels);
        require(before.m_trace.m_events == after.m_trace.m_events, "History read/binding calls differ");
        const auto oldView = baseline::GpuGeometryOcclusion::CaptureView(before.m_context, before.m_projection);
        const auto newView = current::GpuGeometryOcclusion::CaptureView(after.m_context, after.m_projection);
        oldPyramid->RequireView(oldView);
        newPyramid->RequireView(newView);
        require(rejects([&] { oldPyramid->RequireCurrentView(oldView); }) &&
                    rejects([&] { newPyramid->RequireCurrentView(newView); }),
                "Retained history accepted as current depth");
        require(rejects([&] { oldPyramid->Declare(before.m_graph, before.Depth()); }) &&
                    rejects([&] { newPyramid->Declare(after.m_graph, after.Depth()); }),
                "History overwrite accepted");
    }

    void compare_writer(unsigned levels, bool history, unsigned failUpload = 0, unsigned failBinding = 0)
    {
        Services before(levels), after(levels);
        const auto oldPyramid = make_writer<baseline::GpuGeometryOcclusion>(before, levels, history);
        const auto newPyramid = make_writer<current::GpuGeometryOcclusion>(after, levels, history);
        oldPyramid->Declare(before.m_graph, before.Depth());
        newPyramid->Declare(after.m_graph, after.Depth());
        require(before.m_trace.m_events == after.m_trace.m_events, "HZB declaration/version/phase order differs");
        const auto count = before.m_trace.m_events.size();
        oldPyramid->Declare(before.m_graph, before.Depth());
        newPyramid->Declare(after.m_graph, after.Depth());
        require(count == before.m_trace.m_events.size() && count == after.m_trace.m_events.size(),
                "Repeated HZB declaration issued more work");
        auto changed = before.Depth();
        ++changed.version;
        require(rejects([&] { oldPyramid->Declare(before.m_graph, changed); }) &&
                    rejects([&] { newPyramid->Declare(after.m_graph, changed); }),
                "Different HZB input version accepted");
        before.m_device.m_failUpload = after.m_device.m_failUpload = failUpload;
        before.m_device.m_failBinding = after.m_device.m_failBinding = failBinding;
        const bool oldFailure = rejects([&] { before.m_graph.Execute(before.m_encoder); });
        const bool newFailure = rejects([&] { after.m_graph.Execute(after.m_encoder); });
        require(oldFailure == newFailure && oldFailure == bool(failUpload || failBinding),
                "HZB deferred upload/binding failure differs");
        require(before.m_trace.m_events == after.m_trace.m_events,
                "HZB deferred dispatch/binding/constant order differs");
        if (history)
        {
            require(oldPyramid->m_history->recorded.load() == newPyramid->m_history->recorded.load() &&
                        newPyramid->m_history->recorded.load() == !newFailure,
                    "History recorded publication differs");
        }
        before.m_device.m_failUpload = after.m_device.m_failUpload = 0;
        before.m_device.m_failBinding = after.m_device.m_failBinding = 0;
        exercise_reads(oldPyramid, before, levels);
        exercise_reads(newPyramid, after, levels);
        require(before.m_trace.m_events == after.m_trace.m_events, "HZB writer consumer calls differ");
    }

    void invalid_history_tests()
    {
        using Visibility = current::GpuGeometryOcclusion;
        for (unsigned levels : {0u, 17u})
        {
            Services services(4);
            const auto history = make_history<Visibility>(services, levels);
            require(rejects([&] { Visibility::ReadHistory(services.m_context, services.m_projection, history); }),
                    "Invalid history level capacity accepted");
            require(services.m_trace.m_events.empty(), "Invalid history reached graph services");
        }
        for (unsigned size : {0u, 1u, 17u})
        {
            Services services(4);
            const auto history = make_history<Visibility>(services, 4);
            const auto pyramid = Visibility::ReadHistory(services.m_context, services.m_projection, history);
            history->textures.resize(size);
            require(rejects([&] { pyramid->ImportHistory(services.m_graph); }), "Changed history size accepted");
            require(services.m_trace.m_events.empty(), "Changed history reached graph services");
        }
        for (unsigned condition = 0; condition < 10; ++condition)
        {
            Services services(4);
            auto history = make_history<Visibility>(services, 4);
            switch (condition)
            {
            case 0:
                history.reset();
                break;
            case 1:
                history->recorded = false;
                break;
            case 2:
                history->completion = 0;
                break;
            case 3:
                history->device = nullptr;
                break;
            case 4:
                ++history->source.width;
                break;
            case 5:
                ++history->source.height;
                break;
            case 6:
                ++history->source.sceneEpoch;
                break;
            case 7:
                history->source.frameId = services.m_context.frameId;
                break;
            case 8:
                history->source.frameId = services.m_context.frameId + 1;
                break;
            case 9:
                services.m_context.resources = nullptr;
                break;
            }
            require(rejects([&] { Visibility::ReadHistory(services.m_context, services.m_projection, history); }),
                    "Invalid immutable history identity accepted");
        }
        Services services(4), other(4);
        const auto history = make_history<Visibility>(services, 4);
        const auto pyramid = Visibility::ReadHistory(services.m_context, services.m_projection, history);
        require(rejects([&] { pyramid->ImportHistory(other.m_graph); }), "Different graph device accepted");
        services.m_graph.m_mode = RGSchedulingMode::DeclarationOrder;
        require(rejects([&] { pyramid->ImportHistory(services.m_graph); }), "Unversioned history graph accepted");
        services.m_graph.m_mode = RGSchedulingMode::ExplicitVersioned;
        pyramid->ImportHistory(services.m_graph);
        EnhancedRenderGraph another(services.m_trace, services.m_device);
        require(rejects([&] { pyramid->ImportHistory(another); }), "Different same-device graph accepted");
        auto view = Visibility::CaptureView(services.m_context, services.m_projection);
        ++view.projectionBits[0];
        require(rejects([&] { pyramid->RequireView(view); }), "Different view projection accepted");
    }

    template<class Visibility>
    std::array<size_t, 2> warm_allocations()
    {
        Services services(16);
        const auto history = make_history<Visibility>(services, 16);
        std::array<size_t, 2> allocations{};
        for (unsigned sample = 0; sample < 33; ++sample)
        {
            services.m_trace.m_events.clear();
            services.m_graph.m_count = 1;
            services.m_graph.m_importCount = 0;
            allocation_count = 0;
            count_allocations = sample >= 16;
            const auto pyramid = Visibility::ReadHistory(services.m_context, services.m_projection, history);
            const auto prepare = allocation_count;
            allocation_count = 0;
            pyramid->ImportHistory(services.m_graph);
            count_allocations = false;
            const std::array value{prepare, allocation_count};
            if (sample >= 16)
            {
                if (sample == 16)
                {
                    allocations = value;
                }
                require(allocations == value, "HZB warm allocation count unstable");
            }
        }
        return allocations;
    }
} // namespace

int main()
{
    try
    {
        size_t scenarios{};
        for (unsigned levels = 1; levels <= 16; ++levels)
        {
            compare_history(levels, 0);
            compare_writer(levels, false);
            compare_writer(levels, true);
            scenarios += 3;
            for (unsigned failure = 1; failure <= levels; ++failure)
            {
                compare_history(levels, failure);
                ++scenarios;
            }
        }
        for (unsigned failure = 1; failure <= 16; ++failure)
        {
            compare_writer(16, true, failure);
            ++scenarios;
        }
        for (unsigned failure = 1; failure <= 32; ++failure)
        {
            compare_writer(16, true, 0, failure);
            ++scenarios;
        }
        invalid_history_tests();
        const auto before = warm_allocations<baseline::GpuGeometryOcclusion>();
        const auto after = warm_allocations<current::GpuGeometryOcclusion>();
        require(after[1] == 0 && after[0] + after[1] < before[0] + before[1],
                "HZB allocation work moved instead of decreasing");
        std::cout << "{\"result\":\"pass\",\"checks\":" << checks << ",\"parity_scenarios\":" << scenarios
                  << ",\"warmup\":16,\"samples\":17,\"levels\":16,\"baseline_read_history_allocations\":" << before[0]
                  << ",\"current_read_history_allocations\":" << after[0]
                  << ",\"baseline_import_allocations\":" << before[1] << ",\"current_import_allocations\":" << after[1]
                  << "}\n";
    }
    catch (const std::exception& error)
    {
        count_allocations = false;
        std::cerr << error.what() << '\n';
        return 1;
    }
}
