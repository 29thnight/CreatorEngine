// The runner supplies exact baseline/current SSGI source and production graph
// ownership templates. Only the graph/RHI service boundary is substituted.
#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <new>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include "Engine/RenderEngine/RenderArena.h"

namespace
{
    bool count_allocations{};
    size_t allocation_count{};
    size_t checks{};

    void require(bool condition, const char* message)
    {
        ++checks;
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }
} // namespace

void ssgi_count_allocation()
{
    allocation_count += count_allocations;
}

void* operator new(size_t bytes)
{
    ssgi_count_allocation();
    if (auto* value = std::malloc(bytes ? bytes : 1))
    {
        return value;
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
void* operator new(size_t bytes, std::align_val_t alignment)
{
    ssgi_count_allocation();
    void* value{};
    if (posix_memalign(&value, static_cast<size_t>(alignment), bytes ? bytes : 1) != 0)
    {
        throw std::bad_alloc();
    }
    return value;
}
void operator delete(void* value, std::align_val_t) noexcept
{
    std::free(value);
}
void operator delete(void* value, size_t, std::align_val_t) noexcept
{
    std::free(value);
}

namespace math
{
    struct matrix4x4
    {
        float m[4][4]{};
        static matrix4x4 identity()
        {
            matrix4x4 value;
            for (unsigned index = 0; index < 4; ++index)
            {
                value.m[index][index] = 1;
            }
            return value;
        }
        bool operator==(const matrix4x4&) const = default;
    };
    matrix4x4 transpose(const matrix4x4& source)
    {
        matrix4x4 result;
        for (unsigned row = 0; row < 4; ++row)
        {
            for (unsigned column = 0; column < 4; ++column)
            {
                result.m[row][column] = source.m[column][row];
            }
        }
        return result;
    }
    matrix4x4 operator*(const matrix4x4& left, const matrix4x4& right)
    {
        matrix4x4 result;
        for (unsigned row = 0; row < 4; ++row)
        {
            for (unsigned column = 0; column < 4; ++column)
            {
                for (unsigned item = 0; item < 4; ++item)
                {
                    result.m[row][column] += left.m[row][item] * right.m[item][column];
                }
            }
        }
        return result;
    }
} // namespace math

enum class RHIFormat
{
    RGBA8Unorm,
    RGBA16Float,
    R32Float,
    D32Float
};
enum class RHIResourceState
{
    Common,
    ShaderResource,
    UnorderedAccess,
    CopySource,
    CopyDest
};
enum class RGAccessMode
{
    LegacyState,
    Read,
    Write,
    ReadWrite
};
enum class RGSchedulingMode
{
    DeclarationOrder,
    ExplicitSingleWriter,
    ExplicitVersioned
};
enum class RGResourceKind
{
    Texture,
    Buffer
};
struct RGHandle
{
    uint16_t index{0xffff};
    uint16_t version{};
    RGResourceKind kind{RGResourceKind::Texture};
    uint64_t epoch{};
    bool IsValid() const { return index != 0xffff; }
    bool operator==(const RGHandle&) const = default;
};
struct RHITextureHandle
{
    uint32_t id{};
    bool IsValid() const { return id != 0; }
    bool operator==(const RHITextureHandle&) const = default;
};
struct RHIPipelineHandle
{
    uint32_t id{};
    bool IsValid() const { return id != 0; }
};
struct RHIBindingTable
{
    uint32_t id{};
    bool IsValid() const { return id != 0; }
};
enum class RHIUploadUsage
{
    ConstantBuffer
};
struct RHIUploadRequest
{
    size_t bytes{};
    RHIUploadUsage usage{};
    size_t alignment{};
};
struct RHIBufferSlice
{
    uint32_t id{};
    void* cpuAddress{};
    size_t size{};
    bool IsValid() const { return id != 0; }
};
struct RHIBindingDesc
{
    uint32_t kind{}, resource{}, format{}, depth{};
    static RHIBindingDesc SrvDepth(RHITextureHandle handle) { return {1, handle.id, 0, 1}; }
    static RHIBindingDesc Srv2D(RHITextureHandle handle, RHIFormat format)
    {
        return {1, handle.id, static_cast<uint32_t>(format), 0};
    }
    static RHIBindingDesc Uav2D(RHITextureHandle handle, RHIFormat format)
    {
        return {2, handle.id, static_cast<uint32_t>(format), 0};
    }
};
struct RGTextureDesc
{
    uint32_t width{}, height{}, arraySize{1};
    RHIFormat format{RHIFormat::RGBA8Unorm};
    bool allowRenderTarget{}, allowDepthStencil{}, allowUnorderedAccess{};
    std::string name;
};

struct Event
{
    std::array<char, 64> m_name{};
    std::array<uint64_t, 8> m_values{};
    std::array<std::byte, 512> m_bytes{};
    size_t m_byteCount{};
    bool operator==(const Event&) const = default;
};
class Trace
{
  public:
    std::vector<Event> m_events;
    Trace() { m_events.reserve(512); }
    template<class... Values>
    Event& Add(std::string_view name, Values... values)
    {
        require(name.size() < Event{}.m_name.size(), "Trace name exceeds fixed storage");
        m_events.emplace_back();
        auto& event = m_events.back();
        std::copy(name.begin(), name.end(), event.m_name.begin());
        event.m_values = {static_cast<uint64_t>(values)...};
        return event;
    }
    void Bytes(Event& event, const void* data, size_t bytes)
    {
        require(bytes <= event.m_bytes.size(), "Trace payload exceeds fixed storage");
        event.m_byteCount = bytes;
        if (bytes)
        {
            std::memcpy(event.m_bytes.data(), data, bytes);
        }
    }
    void Reset() { m_events.clear(); }
};

class Device
{
  public:
    struct Description
    {
        uint32_t width{}, height{};
        RHIFormat format{RHIFormat::RGBA16Float};
    };
    Trace& m_trace;
    std::array<Description, 512> m_descriptions;
    std::array<std::array<std::byte, 512>, 64> m_uploads;
    uint32_t m_uploadCount{}, m_bindingCount{}, m_failUpload{}, m_failBinding{};
    explicit Device(Trace& trace) : m_trace(trace) {}
    void Reset(uint32_t failUpload = 0, uint32_t failBinding = 0)
    {
        m_uploadCount = m_bindingCount = 0;
        m_failUpload = failUpload;
        m_failBinding = failBinding;
    }
    RHIBufferSlice AllocateUpload(RHIUploadRequest request)
    {
        const auto serial = ++m_uploadCount;
        m_trace.Add("AllocateUpload", serial, request.bytes, request.usage, request.alignment);
        require(serial < m_uploads.size() && request.bytes <= m_uploads[serial].size(), "Upload fixture capacity");
        if (serial == m_failUpload)
        {
            return {};
        }
        std::fill(m_uploads[serial].begin(), m_uploads[serial].end(), std::byte{0xcc});
        return {serial, m_uploads[serial].data(), request.bytes};
    }
    RHIBufferSlice UploadConstants(const void* data, size_t bytes)
    {
        auto& event = m_trace.Add("UploadConstants", bytes);
        m_trace.Bytes(event, data, bytes);
        const auto result = AllocateUpload({bytes, RHIUploadUsage::ConstantBuffer, 256});
        if (result.IsValid())
        {
            std::memcpy(result.cpuAddress, data, bytes);
        }
        return result;
    }
    RHIBindingTable CreateBindings(std::span<const RHIBindingDesc> values)
    {
        const auto serial = ++m_bindingCount;
        auto& event = m_trace.Add("CreateBindings", serial, values.size());
        m_trace.Bytes(event, values.data(), values.size_bytes());
        return {serial == m_failBinding ? 0u : serial};
    }
    Description DescribeTexture(RHITextureHandle handle)
    {
        m_trace.Add("DescribeTexture", handle.id);
        return m_descriptions.at(handle.id);
    }
    void ReleaseTexture(RHITextureHandle) {}
};
enum class RHIBindPoint
{
    Compute
};
class RHIEncoder
{
  public:
    Trace& m_trace;
    explicit RHIEncoder(Trace& trace) : m_trace(trace) {}
    void SetPipeline(RHIBindPoint bind, RHIPipelineHandle pipeline) { m_trace.Add("SetPipeline", bind, pipeline.id); }
    void SetConstantBuffer(RHIBindPoint bind, uint32_t root, RHIBufferSlice value)
    {
        auto& event = m_trace.Add("SetConstantBuffer", bind, root, value.id, value.size);
        m_trace.Bytes(event, value.cpuAddress, value.size);
    }
    void SetBindings(RHIBindPoint bind, uint32_t root, RHIBindingTable table)
    {
        m_trace.Add("SetBindings", bind, root, table.id);
    }
    void Dispatch(uint32_t x, uint32_t y, uint32_t z) { m_trace.Add("Dispatch", x, y, z); }
    void CopyResource(RHITextureHandle target, RHITextureHandle source)
    {
        m_trace.Add("CopyResource", target.id, source.id);
    }
};

class EnhancedRenderGraph
{
  public:
    struct RGPassUsage
    {
        RGHandle handle;
        RHIResourceState state{RHIResourceState::Common};
        RGAccessMode access{RGAccessMode::LegacyState};
        bool operator==(const RGPassUsage&) const = default;
    };
    struct ExecuteContext
    {
        const EnhancedRenderGraph* graph{};
        RHIEncoder* encoder{};
        RHITextureHandle ResolveHandle(RGHandle handle) const { return graph->ResolveHandle(handle); }
    };
    using ExecuteCallback = std::function<void(const ExecuteContext&)>;
    struct Pass
    {
        std::array<char, 64> m_name;
        std::array<RGPassUsage, 16> m_uses;
        size_t m_useCount{};
        ExecuteCallback m_execute;
    };
    Trace& m_trace;
    Device& m_device;
    RGSchedulingMode m_mode{RGSchedulingMode::ExplicitVersioned};
    std::array<RHITextureHandle, 128> m_resources;
    std::array<uint16_t, 128> m_versions;
    std::array<Pass, 32> m_passes;
    size_t m_resourceCount{}, m_passCount{};
    ce::render_arena_objects m_declarationData;
    explicit EnhancedRenderGraph(Trace& trace, Device& device) : m_trace(trace), m_device(device) {}
    ~EnhancedRenderGraph() { Reset(); }
    void RequireQueueIdle() const {}
#include "GraphOwnership.inl"
    RGSchedulingMode GetSchedulingMode() const { return m_mode; }
    void Reset()
    {
        for (auto& pass : m_passes)
        {
            pass.m_execute = {};
        }
        m_declarationData.reset();
        m_resourceCount = 9;
        m_passCount = 0;
        m_versions.fill(0);
        for (uint32_t index = 0; index < m_resourceCount; ++index)
        {
            m_resources[index] = {index + 10};
        }
    }
    RGHandle CreateTexture(const RGTextureDesc& value)
    {
        require(m_resourceCount < m_resources.size(), "Resource fixture capacity");
        const auto index = static_cast<uint16_t>(m_resourceCount++);
        const RHITextureHandle native{uint32_t(index) + 100};
        m_resources[index] = native;
        m_device.m_descriptions[native.id] = {value.width, value.height, value.format};
        m_trace.Add(value.name, index, value.width, value.height, value.format, value.allowRenderTarget,
                    value.allowDepthStencil, value.allowUnorderedAccess, value.arraySize);
        return {index, 0, RGResourceKind::Texture, 19};
    }
    RGHandle ImportTexture(RHITextureHandle value, RHIResourceState state, std::string_view name,
                           RHIResourceState* stateWriteback = nullptr)
    {
        require(m_resourceCount < m_resources.size(), "Import fixture capacity");
        const auto index = static_cast<uint16_t>(m_resourceCount++);
        m_resources[index] = value;
        m_trace.Add(name, index, value.id, state, stateWriteback != nullptr);
        return {index, 0, RGResourceKind::Texture, 19};
    }
    RGHandle Write(RGHandle value)
    {
        value.version = ++m_versions.at(value.index);
        m_trace.Add("Write", value.index, value.version);
        return value;
    }
    uint32_t AddPass(std::string_view name, std::span<const RGPassUsage> uses, ExecuteCallback execute,
                     bool sideEffect = false)
    {
        require(m_passCount < m_passes.size(), "Pass fixture capacity");
        auto& pass = m_passes[m_passCount];
        require(uses.size() <= pass.m_uses.size(), "Usage fixture capacity");
        pass.m_name.fill(0);
        std::copy(name.begin(), name.end(), pass.m_name.begin());
        pass.m_useCount = uses.size();
        std::copy(uses.begin(), uses.end(), pass.m_uses.begin());
        pass.m_execute = std::move(execute);
        m_trace.Add(name, m_passCount, uses.size(), sideEffect);
        for (const auto& use : uses)
        {
            m_trace.Add("Usage", use.handle.index, use.handle.version, use.handle.kind, use.handle.epoch, use.state,
                        use.access);
        }
        return static_cast<uint32_t>(m_passCount++);
    }
    uint32_t AddPass(std::string_view name, const std::vector<RGPassUsage>& uses, ExecuteCallback execute,
                     bool sideEffect = false)
    {
        return AddPass(name, std::span{uses}, std::move(execute), sideEffect);
    }
    void DeclareComputeCompatible(uint32_t pass) { m_trace.Add("ComputeCompatible", pass); }
    void DeclareRecordingIndependent(uint32_t pass) { m_trace.Add("RecordingIndependent", pass); }
    RHITextureHandle ResolveHandle(RGHandle handle) const
    {
        require(handle.IsValid() && handle.index < m_resourceCount, "Invalid graph resource resolved");
        m_trace.Add("ResolveHandle", handle.index, handle.version, handle.kind, handle.epoch);
        return m_resources[handle.index];
    }
    void Execute(RHIEncoder& encoder)
    {
        const ExecuteContext context{this, &encoder};
        for (size_t index = 0; index < m_passCount; ++index)
        {
            m_trace.Add(m_passes[index].m_name.data(), index);
            m_passes[index].m_execute(context);
        }
    }
};

struct Camera
{
    math::matrix4x4 view{}, projection{}, inverseView{}, inverseProjection{};
};
struct EnhancedFrameContext
{
    Device* resources{};
    Camera* camera{};
    uint32_t width{}, height{};
    bool temporalDiagnosticCapture{};
    struct Temporal
    {
        uint64_t realFrameId{};
        bool reset{};
        float jitterX{}, jitterY{}, previousJitterX{}, previousJitterY{};
        struct TemporalCamera
        {
            bool valid{};
            std::array<float, 16> viewMatrix{}, projectionMatrix{}, clipToPreviousClip{};
        } camera;
    } temporalFrame;
};
class PassResourceRetirement
{
  public:
    std::function<void()> m_accepted;
    template<class Callback>
    void WatchRecording(std::nullptr_t, Callback&& callback)
    {
        m_accepted = std::forward<Callback>(callback);
    }
};
class EnhancedRenderPass
{
  public:
    virtual ~EnhancedRenderPass() = default;
    virtual const char* GetName() const = 0;
    virtual bool Initialize(const EnhancedFrameContext&, std::string&) = 0;
    virtual bool PrepareFrame(const EnhancedFrameContext&, std::string&) = 0;
    virtual void Declare(EnhancedRenderGraph&, const EnhancedFrameContext&) = 0;
    virtual void Shutdown() = 0;
};

// Visibility is relaxed only for fixture state setup. No extracted function or
// class body is rewritten; native include/PSO initialization is outside this gate.
#define private public
namespace baseline
{
#include "BaselineSsgiClass.inl"
#include "BaselineSsgiParams.inl"
#include "BaselineSsgiDeclare.inl"
#include "BaselineSsgiReset.inl"
    bool EnhancedSSGIPass::Initialize(const EnhancedFrameContext&, std::string&)
    {
        return true;
    }
    bool EnhancedSSGIPass::PrepareFrame(const EnhancedFrameContext&, std::string&)
    {
        return true;
    }
    void EnhancedSSGIPass::Shutdown() {}
} // namespace baseline
namespace current
{
#include "CurrentSsgiClass.inl"
#include "CurrentSsgiParams.inl"
#include "CurrentSsgiDeclare.inl"
#include "CurrentSsgiReset.inl"
    bool EnhancedSSGIPass::Initialize(const EnhancedFrameContext&, std::string&)
    {
        return true;
    }
    bool EnhancedSSGIPass::PrepareFrame(const EnhancedFrameContext&, std::string&)
    {
        return true;
    }
    void EnhancedSSGIPass::Shutdown() {}
} // namespace current
#undef private

namespace
{
    struct Scenario
    {
        unsigned mask{255}, mips{8}, width{1186}, height{514};
        unsigned historyIndex{}, failUpload{}, failBinding{};
        RGSchedulingMode mode{RGSchedulingMode::ExplicitVersioned};
        bool camera{true}, history{true}, temporal{true}, reset{}, accepted{true};
        bool diagnostic{}, disabled{}, missingDepth{}, missingPipeline{}, clobber{}, forceGrowth{};
    };

    template<class Pass>
    class Fixture
    {
      public:
        Trace m_trace;
        Device m_device{m_trace};
        EnhancedRenderGraph m_graph{m_trace, m_device};
        RHIEncoder m_encoder{m_trace};
        Camera m_camera;
        EnhancedFrameContext m_context;
        Pass m_pass;

        void Configure(const Scenario& test)
        {
            m_graph.Reset();
            m_graph.m_mode = test.mode;
            m_device.Reset(test.failUpload, test.failBinding);
            m_trace.Reset();
            m_pass.m_resourceRetirement.m_accepted = {};
            for (unsigned row = 0; row < 4; ++row)
            {
                for (unsigned column = 0; column < 4; ++column)
                {
                    const auto value = float(row * 4 + column + 1) * 0.125f;
                    m_camera.view.m[row][column] = value;
                    m_camera.projection.m[row][column] = value * 2.f;
                    m_camera.inverseView.m[row][column] = value * 3.f;
                    m_camera.inverseProjection.m[row][column] = value * 4.f;
                }
            }
            m_context = {};
            m_context.resources = &m_device;
            m_context.camera = test.camera ? &m_camera : nullptr;
            m_context.width = test.width;
            m_context.height = test.height;
            m_context.temporalDiagnosticCapture = test.diagnostic;
            m_context.temporalFrame.realFrameId = test.temporal ? 31 : 0;
            m_context.temporalFrame.reset = test.reset;
            m_context.temporalFrame.jitterX = .25f;
            m_context.temporalFrame.jitterY = -.125f;
            m_context.temporalFrame.previousJitterX = -.375f;
            m_context.temporalFrame.previousJitterY = .5f;
            m_context.temporalFrame.camera.valid = test.temporal;
            m_context.temporalFrame.camera.viewMatrix = std::bit_cast<std::array<float, 16>>(m_camera.view);
            m_context.temporalFrame.camera.projectionMatrix = std::bit_cast<std::array<float, 16>>(m_camera.projection);
            m_context.temporalFrame.camera.clipToPreviousClip =
                std::bit_cast<std::array<float, 16>>(m_camera.inverseView);
            m_pass.m_inputs = {};
            m_pass.m_inputs.depth = test.missingDepth ? RGHandle{} : RGHandle{0, 3, RGResourceKind::Texture, 19};
            RGHandle* optional[]{&m_pass.m_inputs.normal,           &m_pass.m_inputs.diffuse,
                                 &m_pass.m_inputs.metalRough,       &m_pass.m_inputs.lighting,
                                 &m_pass.m_inputs.ambientOcclusion, &m_pass.m_inputs.motionVectors,
                                 &m_pass.m_inputs.responsiveMask,   &m_pass.m_inputs.reactiveMask};
            for (uint16_t index = 0; index < std::size(optional); ++index)
            {
                if (test.mask & (1u << index))
                {
                    *optional[index] = {uint16_t(index + 1), uint16_t(index + 2), RGResourceKind::Texture, 19};
                }
            }
            m_pass.m_tuning = {};
            m_pass.m_tuning.intensity = test.disabled ? 0 : 1.375f;
            m_pass.m_giWidth = (std::max)(1u, test.width / 2);
            m_pass.m_giHeight = (std::max)(1u, test.height / 2);
            m_pass.m_hiZMipCount = test.mips;
            m_pass.m_history = {RHITextureHandle{50}, RHITextureHandle{51}};
            m_pass.m_historyDepth = {RHITextureHandle{52}, RHITextureHandle{53}};
            m_pass.m_historyIndex = test.historyIndex;
            m_pass.m_historyValid = m_pass.m_hasPreviousFrame = test.history;
            m_pass.m_historyState.fill(RHIResourceState::ShaderResource);
            m_pass.m_historyDepthState.fill(RHIResourceState::ShaderResource);
            m_pass.m_previousViewProjection = m_camera.inverseProjection;
            m_pass.m_frameIndex = 17;
            m_pass.m_hiZBuildPSO = {1};
            m_pass.m_tracePSO = {test.missingPipeline ? 0u : 2u};
            m_pass.m_resolvePSO = {3};
            m_pass.m_filterPSO = {4};
            m_pass.m_compositePSO = {5};
        }
        void Declare() { m_pass.Declare(m_graph, m_context); }
        void Execute(const Scenario& test)
        {
            if (test.clobber)
            {
                // Trace has always read tuning lazily; Resolve/Filter/Composite
                // constants must retain the values sealed during declaration.
                m_pass.m_tuning.filterDepthSigma = 999.f;
                m_pass.m_tuning.compositeDepthSigma = 999.f;
                m_pass.m_tuning.accumDepthTolerance = 999.f;
                m_pass.m_tuning.traceDistance = 27.f;
            }
            if (test.forceGrowth)
            {
                const std::array<uint8_t, 8192> poison{};
                for (unsigned index = 0; index < 20; ++index)
                {
                    m_graph.CopyDeclaration(std::span<const uint8_t>{poison});
                }
            }
            m_graph.Execute(m_encoder);
            if (test.accepted && m_pass.m_resourceRetirement.m_accepted)
            {
                m_pass.m_resourceRetirement.m_accepted();
            }
        }
    };

    template<class Left, class Right>
    void compare_state(const Left& left, const Right& right)
    {
        require(left.m_output == right.m_output && left.m_traceResult == right.m_traceResult &&
                    left.m_resolved == right.m_resolved && left.m_filtered == right.m_filtered,
                "SSGI output handles differ");
        require(left.m_hiZMips == right.m_hiZMips && left.m_historyHandle == right.m_historyHandle &&
                    left.m_historyDepthHandle == right.m_historyDepthHandle,
                "SSGI mip/history handles differ");
        require(left.m_previousViewProjection == right.m_previousViewProjection &&
                    left.m_frameIndex == right.m_frameIndex && left.m_historyIndex == right.m_historyIndex &&
                    left.m_hasPreviousFrame == right.m_hasPreviousFrame && left.m_historyValid == right.m_historyValid,
                "SSGI temporal acceptance differs");
    }

    void compare(Fixture<baseline::EnhancedSSGIPass>& before, Fixture<current::EnhancedSSGIPass>& after,
                 const Scenario& test)
    {
        before.Configure(test);
        after.Configure(test);
        before.Declare();
        after.Declare();
        require(before.m_trace.m_events == after.m_trace.m_events, "SSGI declaration call/usage order differs");
        compare_state(before.m_pass, after.m_pass);
        before.Execute(test);
        after.Execute(test);
        if (before.m_trace.m_events != after.m_trace.m_events)
        {
            size_t first{};
            while (first < before.m_trace.m_events.size() && first < after.m_trace.m_events.size() &&
                   before.m_trace.m_events[first] == after.m_trace.m_events[first])
            {
                ++first;
            }
            std::cerr << "First execution mismatch " << first << " mask " << test.mask << " mips " << test.mips;
            if (first < before.m_trace.m_events.size())
            {
                std::cerr << " baseline " << before.m_trace.m_events[first].m_name.data();
            }
            if (first < after.m_trace.m_events.size())
            {
                std::cerr << " current " << after.m_trace.m_events[first].m_name.data();
            }
            std::cerr << '\n';
            throw std::runtime_error("SSGI deferred bytes/bindings/commands differ");
        }
        require(true, "SSGI deferred execution parity");
        compare_state(before.m_pass, after.m_pass);
    }

    template<class Pass>
    std::pair<size_t, size_t> warm_allocations(Fixture<Pass>& fixture)
    {
        const Scenario test;
        size_t declarationAllocations{}, executionAllocations{};
        for (unsigned sample = 0; sample < 33; ++sample)
        {
            fixture.Configure(test);
            allocation_count = 0;
            count_allocations = sample >= 16;
            fixture.Declare();
            const auto declaration = allocation_count;
            allocation_count = 0;
            fixture.Execute(test);
            count_allocations = false;
            if (sample >= 16)
            {
                if (sample == 16)
                {
                    declarationAllocations = declaration;
                    executionAllocations = allocation_count;
                }
                require(declarationAllocations == declaration && executionAllocations == allocation_count,
                        "Warm allocation count is unstable");
            }
        }
        return {declarationAllocations, executionAllocations};
    }
} // namespace

int main()
{
    try
    {
        auto before = std::make_unique<Fixture<baseline::EnhancedSSGIPass>>();
        auto after = std::make_unique<Fixture<current::EnhancedSSGIPass>>();
        size_t scenarios{};
        for (const auto mode : {RGSchedulingMode::DeclarationOrder, RGSchedulingMode::ExplicitSingleWriter,
                                RGSchedulingMode::ExplicitVersioned})
        {
            for (unsigned mask = 0; mask < 256; ++mask)
            {
                Scenario test;
                test.mode = mode;
                test.mask = mask;
                test.mips = mask % 8 + 1;
                test.historyIndex = mask % 2;
                test.camera = mask % 3 != 0;
                test.temporal = mask % 5 != 0;
                test.history = mask % 7 != 0;
                test.reset = mask % 11 == 0;
                test.accepted = mask % 13 != 0;
                test.clobber = true;
                test.forceGrowth = mask % 32 == 0;
                test.width = mask % 2 ? 1186 : 1031;
                test.height = mask % 2 ? 514 : 777;
                compare(*before, *after, test);
                ++scenarios;
            }
        }
        for (unsigned branch = 0; branch < 8; ++branch)
        {
            Scenario test;
            test.disabled = branch & 1;
            test.diagnostic = branch & 2;
            test.missingDepth = branch == 4;
            test.missingPipeline = branch == 6;
            test.width = test.height = 1;
            test.mips = 1;
            compare(*before, *after, test);
            ++scenarios;
        }
        for (unsigned failure = 1; failure <= 12; ++failure)
        {
            Scenario test;
            test.failUpload = failure;
            compare(*before, *after, test);
            ++scenarios;
        }
        for (unsigned failure = 1; failure <= 24; ++failure)
        {
            Scenario test;
            test.failBinding = failure;
            compare(*before, *after, test);
            ++scenarios;
        }
        const auto oldAllocations = warm_allocations(*before);
        const auto newAllocations = warm_allocations(*after);
        require(newAllocations.first < oldAllocations.first, "Declaration allocations did not decrease");
        require(newAllocations.second < oldAllocations.second, "Execution staging allocations did not decrease");
        std::cout << "{\"result\":\"pass\",\"checks\":" << checks << ",\"scenarios\":" << scenarios
                  << ",\"warmup\":16,\"samples\":17,\"baseline_declare_allocations\":" << oldAllocations.first
                  << ",\"current_declare_allocations\":" << newAllocations.first
                  << ",\"baseline_execute_allocations\":" << oldAllocations.second
                  << ",\"current_execute_allocations\":" << newAllocations.second << "}\n";
    }
    catch (const std::exception& error)
    {
        count_allocations = false;
        std::cerr << error.what() << '\n';
        return 1;
    }
}
