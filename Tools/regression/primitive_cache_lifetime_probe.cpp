#if defined(CE_CONTRACT_STANDALONE)
#include "../build/ModuleAdapters/Creator.Core.Ownership.h"
#else
import Creator.Core.Ownership;
#endif
// 실제 cache 준비/선언/PSO 선택 본문을 실행한다. 장치·그래프 서비스는 결정적인 CPU 경계다.
#include "RHI/RHIResourceTypes.h"
#include "RHI/RHIPipelineState.h"
#include "RHI/RHIPipelineLayout.h"
#include "RHI/RHIShaderBlob.h"
#include "MaterialGraphPrimitiveCacheContract.h"
#include <latch>
#include <thread>
#include <iostream>

namespace
{
    bool count_allocations{};
    std::size_t allocations{};
    std::atomic<std::size_t> checks{};

    void require(bool condition, const char* message)
    {
        ++checks;
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }

    template<class F>
    void requires_failure(F&& action, const char* message)
    {
        bool failed{};
        try
        {
            action();
        }
        catch (const std::runtime_error&)
        {
            failed = true;
        }
        require(failed, message);
    }
} // namespace

void* operator new(std::size_t bytes)
{
    if (count_allocations)
    {
        ++allocations;
    }
    if (auto* memory = std::malloc(bytes ? bytes : 1))
    {
        return memory;
    }
    throw std::bad_alloc();
}
void* operator new[](std::size_t bytes)
{
    return ::operator new(bytes);
}
void operator delete(void* pointer) noexcept
{
    std::free(pointer);
}
void operator delete[](void* pointer) noexcept
{
    std::free(pointer);
}
void operator delete(void* pointer, std::size_t) noexcept
{
    std::free(pointer);
}
void operator delete[](void* pointer, std::size_t) noexcept
{
    std::free(pointer);
}

namespace math
{
    struct alignas(16) matrix4x4
    {
        std::array<float, 16> m_values{};
    };
    matrix4x4 transpose(const matrix4x4& source)
    {
        matrix4x4 result;
        for (std::size_t row = 0; row < 4; ++row)
        {
            for (std::size_t column = 0; column < 4; ++column)
            {
                result.m_values[row * 4 + column] = source.m_values[column * 4 + row];
            }
        }
        return result;
    }
} // namespace math

#include "PrimitiveCacheGraphTypes.inl"
enum class RHIBindPoint
{
    Graphics,
    Compute
};

struct RHIEncoder
{
    RHIPipelineHandle m_pipeline;
    std::array<RHIBindingTable, 8> m_tables{};
    std::array<RHIBufferSlice, 8> m_constants{};
    std::array<std::uint64_t, 8> m_indirectOffsets{};
    std::size_t m_dispatches{}, m_indirectDispatches{};
    bool m_failIndirect{};
    void SetPipeline(RHIBindPoint, RHIPipelineHandle pipeline) { m_pipeline = pipeline; }
    void SetBindings(RHIBindPoint, std::uint32_t root, RHIBindingTable table) { m_tables.at(root) = table; }
    void SetConstantBuffer(RHIBindPoint, std::uint32_t root, RHIBufferSlice values) { m_constants.at(root) = values; }
    void Dispatch(std::uint32_t x, std::uint32_t y, std::uint32_t z)
    {
        require(x == 256 && y == 1 && z == 1, "Key reset covers all 16384 slots exactly once");
        ++m_dispatches;
    }
    bool DispatchIndirect(RHIBufferHandle, std::uint64_t offset)
    {
        m_indirectOffsets.at(m_indirectDispatches++) = offset;
        return !m_failIndirect;
    }
};

class EnhancedRenderGraph
{
  public:
    struct RGPassUsage
    {
        RGHandle handle;
        RHIResourceState state;
        RGAccessMode access;
    };
    struct ExecuteContext
    {
        const EnhancedRenderGraph* graph;
        RHIEncoder* encoder;
        RHITextureHandle ResolveHandle(RGHandle) const { return {1}; }
    };
    struct pass
    {
        std::string name;
        std::vector<RGPassUsage> uses;
        std::function<void(const ExecuteContext&)> execute;
    };
    std::vector<pass> m_passes;
    std::uint64_t m_epoch{1};
    std::uint16_t m_nextResource{100};
    RGSchedulingMode m_mode{RGSchedulingMode::ExplicitVersioned};
    std::size_t m_failPass{};
    std::function<void()> m_beforeImport;
    std::uint64_t ResourceEpoch() const { return m_epoch; }
    RGSchedulingMode GetSchedulingMode() const { return m_mode; }
    RGHandle ImportBuffer(RHIBufferHandle, RHIResourceState, const char*)
    {
        if (m_beforeImport)
        {
            m_beforeImport();
        }
        return {m_nextResource++, 0, RGResourceKind::Buffer, m_epoch};
    }
    void RequireImportedFinalState(RGHandle, RHIResourceState state)
    {
        require(state == RHIResourceState::Common, "Pooled storage returns to Common");
    }
    RGHandle Write(RGHandle value)
    {
        ++value.version;
        return value;
    }
    RGHandle Modify(RGHandle value)
    {
        ++value.version;
        return value;
    }
    template<class F>
    RGPassId AddPass(const std::string& name, const std::vector<RGPassUsage>& uses, F&& callback)
    {
        if (m_failPass == m_passes.size() + 1)
        {
            throw std::runtime_error("Injected graph population failure");
        }
        m_passes.push_back({name, uses, std::forward<F>(callback)});
        return {static_cast<std::uint16_t>(m_passes.size() - 1)};
    }
    template<class F>
    RGPassId AddPass(const std::string& name, std::initializer_list<RGPassUsage> uses, F&& callback)
    {
        m_passes.push_back({name, {uses.begin(), uses.end()}, std::forward<F>(callback)});
        return {static_cast<std::uint16_t>(m_passes.size() - 1)};
    }
    void DeclareRecordingIndependent(RGPassId) {}
};

class IRenderDeviceServices
{
  public:
    std::array<std::uint64_t, 128> m_bufferBytes{};
    std::array<std::array<RHIBindingDesc, 6>, 128> m_tables{};
    std::array<std::size_t, 128> m_tableSizes{};
    std::array<std::byte, 4096> m_upload{};
    std::size_t m_uploadBytes{}, m_tableCount{}, m_createCalls{}, m_releaseCalls{}, m_failTable{};
    std::uint32_t m_nextBuffer{10};
    std::uint64_t m_recording{1}, m_descriptors{7};
    bool m_changeRecordingOnBinding{};
    std::uint64_t GetCurrentUploadRecordingId() const { return m_recording; }
    std::uint64_t GetDescriptorVersionToken() const { return m_descriptors; }
    RHIBufferSlice UploadConstants(const void* data, std::size_t bytes)
    {
        const auto offset = m_uploadBytes;
        m_uploadBytes += bytes;
        require(m_uploadBytes <= m_upload.size(), "Fixture constants capacity");
        std::memcpy(m_upload.data() + offset, data, bytes);
        RHIBufferSlice result;
        result.buffer = {1};
        result.offset = offset;
        result.size = bytes;
        result.cpuAddress = m_upload.data() + offset;
        return result;
    }
    RHIBufferHandle create_buffer(std::uint64_t bytes)
    {
        ++m_createCalls;
        const auto id = m_nextBuffer++;
        m_bufferBytes.at(id) = bytes;
        return {id};
    }
    RHIBindingTable CreateBindings(std::span<const RHIBindingDesc> descriptions)
    {
        const auto index = m_tableCount++;
        require(index < m_tables.size() && descriptions.size() <= 6, "Fixture descriptor capacity");
        for (std::size_t item = 0; item < descriptions.size(); ++item)
        {
            const auto& description = descriptions[item];
            if (description.dim == RHIBindingDesc::Dim::Buffer)
            {
                const auto bytes =
                    std::uint64_t{description.numElements} * std::max(description.structureByteStride, 4u);
                require(bytes <= m_bufferBytes.at(description.bufferResource.id), "Descriptor fits owned storage");
            }
            m_tables[index][item] = description;
        }
        m_tableSizes[index] = descriptions.size();
        if (m_changeRecordingOnBinding)
        {
            ++m_recording;
        }
        if (m_failTable == m_tableCount)
        {
            return {};
        }
        return {index + 1, static_cast<std::uint32_t>(descriptions.size()), m_descriptors};
    }
};

struct IRenderRootSignatureCache
{
    RHIPipelineLayoutHandle GetOrCreate(const RHIPipelineLayoutDesc&, std::string&) { return {1}; }
};
struct IRenderPipelineCache
{
    std::uint32_t m_meshCreates{};
    RHIMeshPipelineDesc m_lastMesh;
    bool m_failMesh{};
    RHIPipelineHandle GetOrCreateMesh(const RHIMeshPipelineDesc& description, std::string&)
    {
        m_lastMesh = description;
        ++m_meshCreates;
        return m_failMesh ? RHIPipelineHandle{} : RHIPipelineHandle{1000 + m_meshCreates};
    }
};
struct EnhancedFrameContext
{
    IRenderDeviceServices* resources{};
    IRenderRootSignatureCache* rootSignatures{};
    IRenderPipelineCache* psoManager{};
    std::uint32_t width{8}, height{8};
};
struct RHIShaderCompileOptions
{
    bool strictMath{};
};
namespace RHIShaderSource
{
    std::filesystem::path Resolve(std::string_view name)
    {
        return name;
    }
} // namespace RHIShaderSource
namespace LX::Runtime
{
    struct CompiledCompute
    {
        struct stage
        {
            RHIShaderBlob bytecode;
        } stage;
        int description{};
    };
    struct ComputeGeneration
    {
        RHIPipelineHandle GetHandle() const { return {5}; }
    };
    struct ComputePipeline
    {
        std::shared_ptr<const ComputeGeneration> m_generation;
        bool IsValid() const { return bool(m_generation); }
        auto GetGeneration() const { return m_generation; }
        bool Create(IRenderPipelineCache&, const RHIComputePipelineDesc&, int, std::string&)
        {
            m_generation = std::make_shared<ComputeGeneration>();
            return true;
        }
    };
    struct GraphicsPipeline
    {
        RHIPipelineHandle m_handle{11};
        RHIGraphicsPipelineDesc m_description;
        RHIPipelineHandle GetHandle() const { return m_handle; }
        bool IsValid() const { return m_handle.IsValid(); }
        const RHIGraphicsPipelineDesc& GetDesc() const { return m_description; }
    };
    bool CompileCompute(const std::string&, const char*, std::initializer_list<int>,
                        const RHIShaderCompileOptions& options, CompiledCompute& result, std::string&, const char*)
    {
        require(options.strictMath, "Cache uses precise arithmetic compilation");
        constexpr std::uint32_t payload = 1;
        result.stage.bytecode.Assign(&payload, sizeof(payload));
        return true;
    }
} // namespace LX::Runtime

namespace material_graph
{
    struct Generation
    {
        std::uint32_t m_identity{};
    };
    struct TransientBufferPool
    {
        IRenderDeviceServices& m_device;
        explicit TransientBufferPool(IRenderDeviceServices& device) : m_device(device) {}
        template<std::size_t N>
        bool AcquireBatch(const std::array<RHIBufferDesc, N>& descriptions, std::array<RHIBufferHandle, N>& buffers,
                          std::string&)
        {
            for (std::size_t index = 0; index < N; ++index)
            {
                buffers[index] = m_device.create_buffer(descriptions[index].bytes);
            }
            return true;
        }
    };
    void ReleaseSceneBuffer(IRenderDeviceServices& device, const std::shared_ptr<TransientBufferPool>&,
                            RHIBufferHandle buffer)
    {
        if (buffer.IsValid())
        {
            ++device.m_releaseCalls;
        }
    }
    struct resident_buffer
    {
        RHIBufferHandle m_buffer;
        std::uint64_t m_bytes{};
        RHIBufferHandle Buffer() const { return m_buffer; }
        std::uint64_t Size() const { return m_bytes; }
    };
    struct SceneGpuData
    {
        static constexpr std::uint32_t kGeometryPageSize = 64;
        struct Record
        {
            std::array<std::byte, 224> bytes;
        };
        struct Page
        {
            RHIBindingTable vertexTable{1, 64, 7}, topologyTable{2, 64, 7};
        };
        std::vector<Record> records = std::vector<Record>(2);
        std::vector<std::uint32_t> recordPages{UINT32_MAX, 0};
        std::vector<Page> pages = std::vector<Page>(1);
        std::shared_ptr<const resident_buffer> recordBuffer, materialBuffer;
        void AddRead(const EnhancedRenderGraph& graph, std::vector<EnhancedRenderGraph::RGPassUsage>& uses) const
        {
            uses.push_back({{1, 1, RGResourceKind::Buffer, graph.ResourceEpoch()},
                            RHIResourceState::ShaderResource,
                            RGAccessMode::Read});
        }
#include "PrimitiveCachePacketBindings.inl"
    };
    struct SceneMaterialWork
    {
        struct packet_work
        {
            struct values
            {
                std::uint32_t dispatchWidth{65535}, sliceCapacity{65535};
            } values;
            struct bucket
            {
                std::uint32_t first{}, count{};
            };
            struct slice
            {
                std::uint64_t argumentOffset{};
                RHIBufferSlice selector;
            };
            std::array<bucket, 1> buckets{{{0, 2}}};
            std::array<slice, 2> slices{{{0, {}}, {12, {}}}};
        };
        struct bucket
        {
            std::size_t representative{};
        };
        std::array<bucket, 1> buckets{};
        std::unique_ptr<packet_work> packetWork = std::make_unique<packet_work>();
        RHIBufferHandle work, ranges, arguments;
        mutable bool m_declared{true};
        std::uint32_t PacketCapacity() const { return 64; }
        std::array<RGHandle, 2> GraphInputs(const EnhancedRenderGraph& graph) const
        {
            if (!m_declared)
            {
                throw std::runtime_error("Missing packet producer");
            }
            return {{{2, 3, RGResourceKind::Buffer, graph.ResourceEpoch()},
                     {3, 4, RGResourceKind::Buffer, graph.ResourceEpoch()}}};
        }
        void AddRead(EnhancedRenderGraph&, std::vector<EnhancedRenderGraph::RGPassUsage>& uses,
                     const std::array<RGHandle, 2>& inputs) const
        {
            uses.push_back({inputs[0], RHIResourceState::ShaderResource, RGAccessMode::Read});
            uses.push_back({inputs[1], RHIResourceState::IndirectArgument, RGAccessMode::Read});
        }
    };
    void NormalizeSceneReads(const EnhancedRenderGraph&, std::vector<EnhancedRenderGraph::RGPassUsage>&,
                             std::vector<std::size_t>&)
    {
    }
} // namespace material_graph

#include "PrimitiveCacheProduction.inl"

namespace material_graph
{
    struct fixture_program
    {
        own::shared_owner<const Generation> generation = own::make_shared<const Generation>();
        std::array<LX::Runtime::GraphicsPipeline, 2> gbuffer, temporal;
        RHIShaderBlob reconstructShader, m_cachedSurfaceShader, m_cachedTemporalShader;
        std::map<std::uint32_t, RHIPipelineHandle> reconstructionPipelines{{11, {111}}, {12, {112}}};
        std::uint32_t visibilityRoot{1};
        int packetRoots{};
        fixture_program()
        {
            constexpr std::uint32_t data = 42;
            reconstructShader.Assign(&data, sizeof(data));
            m_cachedSurfaceShader = reconstructShader;
            m_cachedTemporalShader = reconstructShader;
            temporal[0].m_handle = {12};
            temporal[0].m_description.depthEnable = true;
            temporal[0].m_description.depthWriteMask = RHIDepthWrite::All;
        }
    };
    struct fixture_input
    {
        math::matrix4x4 ViewProjection() const { return {}; }
    };
    struct fixture_frame
    {
        struct Draw
        {
            own::shared_owner<const fixture_program> program;
            std::uint32_t geometryRecord{1};
            bool doubleSided{};
            RHIPipelineHandle m_cachedSurfacePipeline, m_cachedTemporalPipeline;
        };
        std::vector<Draw> draws;
        std::shared_ptr<const ScenePrimitiveCacheFrame> m_primitiveCache;
        struct material_stage
        {
            std::shared_ptr<const SceneMaterialWork> materialWork;
        } materialWorkStage;
        std::shared_ptr<const fixture_input> input = std::make_shared<fixture_input>();
        RHIBindingTable materialPacketBindings;
        IRenderDeviceServices* device{};
        std::uint64_t recording{1}, descriptors{7};
        void CheckCurrent(const EnhancedRenderGraph* = nullptr) const
        {
            require(device->m_recording == recording && device->m_descriptors == descriptors,
                    "Frame recording retained");
        }
        void BindPacketResources(RHIEncoder&, const Draw&, RHIBindingTable, int) const {}
#include "PrimitiveCacheBindReconstruction.inl"
    };
    struct fixture_host
    {
        struct budget
        {
            bool m_primitiveCache{};
        };
        IRenderDeviceServices* device_{};
        std::shared_ptr<TransientBufferPool> bufferPool_;
        std::shared_ptr<ScenePrimitiveCacheResources> m_primitiveCacheResources;
        bool prepare(const EnhancedFrameContext& context, const own::shared_owner<fixture_frame>& candidate,
                     const std::shared_ptr<SceneGpuData>& sceneData, const budget& budget, std::string& error)
        {
#include "PrimitiveCacheAdmission.inl"
            return true;
        }
    };
} // namespace material_graph

namespace
{
    struct fixture
    {
        IRenderDeviceServices device;
        IRenderRootSignatureCache roots;
        IRenderPipelineCache pipelines;
        EnhancedFrameContext context{&device, &roots, &pipelines};
        material_graph::fixture_host host{&device, std::make_shared<material_graph::TransientBufferPool>(device)};
        std::shared_ptr<material_graph::SceneGpuData> scene = std::make_shared<material_graph::SceneGpuData>();
        std::shared_ptr<material_graph::SceneMaterialWork> material =
            std::make_shared<material_graph::SceneMaterialWork>();
        own::shared_owner<material_graph::fixture_frame> frame = own::make_shared<material_graph::fixture_frame>();
        std::string error;
        fixture()
        {
            scene->recordBuffer = std::make_shared<material_graph::resident_buffer>(device.create_buffer(448), 448);
            scene->materialBuffer = std::make_shared<material_graph::resident_buffer>(device.create_buffer(512), 512);
            material->work = device.create_buffer(512);
            material->ranges = device.create_buffer(16);
            material->arguments = device.create_buffer(24);
            frame->device = &device;
            frame->draws.push_back({own::make_shared<const material_graph::fixture_program>()});
            frame->materialWorkStage.materialWork = material;
        }
        void prepare(bool enabled)
        {
            require(host.prepare(context, frame, scene, {enabled}, error), "Exact frame cache admission succeeds");
        }
    };

    void test_disabled_and_permutation()
    {
        fixture value;
        const auto buffers = value.device.m_createCalls;
        allocations = 0;
        count_allocations = true;
        value.prepare(false);
        count_allocations = false;
        require(allocations == 0 && value.device.m_createCalls == buffers && !value.host.m_primitiveCacheResources &&
                    !value.frame->m_primitiveCache && value.pipelines.m_meshCreates == 0,
                "Default admission allocates no cache CPU/GPU storage or native PSO");
        const auto index = value.frame->materialPacketBindings.backend - 1;
        require(value.device.m_tableSizes[index] == 6 && value.device.m_tables[index][4].numElements == 1 &&
                    value.device.m_tables[index][5].numElements == 1 &&
                    value.device.m_tables[index][5].structureByteStride == 336,
                "Disabled cache uses bounded fallback descriptors in existing table");
        RHIEncoder encoder;
        value.frame->BindReconstruction(encoder, value.frame->draws[0], {1, 1, 7}, {11});
        require(encoder.m_pipeline.id == 111, "Default frame chooses unchanged baseline PSO");
        value.prepare(true);
        require(value.device.m_createCalls == buffers + 2 && value.pipelines.m_meshCreates == 2,
                "Enabled frame prepares exactly two bounded buffers and two distinct reconstruction variants");
        const auto retained = value.frame->m_primitiveCache;
        const auto surface = value.frame->draws[0].m_cachedSurfacePipeline;
        const auto temporal = value.frame->draws[0].m_cachedTemporalPipeline;
        value.frame->BindReconstruction(encoder, value.frame->draws[0], {1, 1, 7}, {11});
        require(encoder.m_pipeline == surface && surface.IsValid(), "Surface chooses the frame's cached PSO");
        value.frame->BindReconstruction(encoder, value.frame->draws[0], {1, 1, 7}, {12});
        require(encoder.m_pipeline == temporal && temporal != surface, "Temporal uses its own cached pixel entry");
        const auto& description = value.pipelines.m_lastMesh;
        require(description.depthEnable && description.depthFunc == RHICompareOp::Always &&
                    description.depthWriteMask == RHIDepthWrite::All && description.cullMode == RHICullMode::None,
                "Cached Temporal preserves reconstruction depth state");
        auto source = value.frame->draws[0].program;
        RHIPipelineHandle again;
        require(value.host.m_primitiveCacheResources->PrepareReconstruction(
                    value.context, source->generation, source->gbuffer[0], source->reconstructShader,
                    source->m_cachedSurfaceShader, false, again, value.error) &&
                    again == surface && value.pipelines.m_meshCreates == 2,
                "Same source PSO and sealed generation reuse the prepared cache variant");
        auto other = own::make_shared<const material_graph::Generation>();
        require(value.host.m_primitiveCacheResources->PrepareReconstruction(
                    value.context, other, source->gbuffer[0], source->reconstructShader, source->m_cachedSurfaceShader,
                    false, again, value.error) &&
                    again != surface && value.pipelines.m_meshCreates == 3,
                "Reused base ID from a different generation cannot reuse stale cache bytecode");
        require(value.frame->draws[0].m_cachedSurfacePipeline == surface && value.frame->m_primitiveCache == retained,
                "An older prepared frame keeps its PSO and data owners across lookup replacement");
    }

    void test_graph_and_retirement()
    {
        fixture value;
        value.prepare(true);
        EnhancedRenderGraph graph;
        const RGHandle visibility{9, 4, RGResourceKind::Texture, graph.ResourceEpoch()};
        auto cache = value.frame->m_primitiveCache;
        std::vector<EnhancedRenderGraph::RGPassUsage> reads;
        requires_failure([&] { cache->AddRead(graph, reads); }, "A consumer cannot invent its missing cache producer");
        cache->Declare(graph, visibility, value.frame);
        require(graph.m_passes.size() == 2, "Cache declares Clear and Populate as two real graph passes");
        const auto& clear = graph.m_passes[0];
        const auto& populate = graph.m_passes[1];
        require(clear.uses.size() == 1 && clear.uses[0].access == RGAccessMode::Write &&
                    populate.uses[0].handle.index == clear.uses[0].handle.index &&
                    populate.uses[0].handle.version == clear.uses[0].handle.version + 1 &&
                    populate.uses[0].access == RGAccessMode::ReadWrite &&
                    populate.uses[1].access == RGAccessMode::Write,
                "Population advances the reset key version and owns the data write");
        cache->AddRead(graph, reads);
        require(reads.size() == 2 && reads[0].handle.version == populate.uses[0].handle.version &&
                    reads[1].handle.index == populate.uses[1].handle.index &&
                    reads[0].state == RHIResourceState::PixelShaderResource,
                "Readers depend on exactly the published key/data versions");
        cache->Declare(graph, visibility, value.frame);
        require(graph.m_passes.size() == 2, "Same recording declaration is idempotent");
        auto wrong = visibility;
        ++wrong.version;
        requires_failure([&] { cache->Declare(graph, wrong, value.frame); },
                         "A different Visibility version is rejected");
        RHIEncoder encoder;
        const EnhancedRenderGraph::ExecuteContext execution{&graph, &encoder};
        graph.m_passes[0].execute(execution);
        graph.m_passes[1].execute(execution);
        require(encoder.m_dispatches == 1 && encoder.m_indirectDispatches == 2 && encoder.m_indirectOffsets[0] == 0 &&
                    encoder.m_indirectOffsets[1] == 12,
                "Population uses each real material argument slice with the compute ABI");
        require(encoder.m_tables[3].count == 64 && encoder.m_tables[4].count == 64,
                "Population binds the bucket's complete current/previous geometry page");
        ++value.device.m_descriptors;
        requires_failure([&] { graph.m_passes[1].execute(execution); }, "Stale descriptors cannot be recorded");
        --value.device.m_descriptors;
        ++graph.m_epoch;
        requires_failure([&] { cache->AddRead(graph, reads); }, "A reset graph cannot reuse cache handles");
        --graph.m_epoch;
        const std::weak_ptr<const material_graph::ScenePrimitiveCacheFrame> weak = cache;
        value.frame.reset();
        cache.reset();
        require(!weak.expired() && value.device.m_releaseCalls == 0,
                "Graph callbacks pin cache storage until retirement");
        graph.m_passes.clear();
        require(weak.expired() && value.device.m_releaseCalls == 2, "Retirement returns both buffers exactly once");
    }

    void test_failures_and_limits()
    {
        fixture value;
        value.material->packetWork->values.dispatchWidth = 65536;
        value.prepare(true);
        require(!value.frame->m_primitiveCache && !value.host.m_primitiveCacheResources,
                "Unportable mesh XYZ limits retain the default shader path before allocating cache storage");
        value.material->packetWork->values.dispatchWidth = 65535;
        value.device.m_failTable = value.device.m_tableCount + 1;
        const auto releases = value.device.m_releaseCalls;
        require(!value.host.prepare(value.context, value.frame, value.scene, {true}, value.error),
                "Failed cache binding rejects preparation");
        require(value.device.m_releaseCalls == releases + 2 && !value.frame->m_primitiveCache,
                "An unpublished failed candidate returns both acquired buffers");
        value.device.m_failTable = 0;
        value.device.m_changeRecordingOnBinding = true;
        require(!value.host.prepare(value.context, value.frame, value.scene, {true}, value.error),
                "Descriptor allocation that crosses recording lifetime rejects preparation");
        require(value.device.m_releaseCalls == releases + 4,
                "Recording failure also releases unpublished cache storage");
    }

    void test_partial_graph_and_shared_scratch()
    {
        fixture value;
        value.prepare(true);
        auto second = own::make_shared<material_graph::fixture_frame>();
        second->device = &value.device;
        second->draws = value.frame->draws;
        second->materialWorkStage.materialWork = value.material;
        require(value.host.prepare(value.context, second, value.scene, {true}, value.error),
                "A second retained frame uses the same persistent cache resources");
        EnhancedRenderGraph firstGraph, secondGraph;
        const RGHandle visibility{9, 4, RGResourceKind::Texture, firstGraph.ResourceEpoch()};
        std::latch firstInside(1), releaseFirst(1), secondStarting(1);
        bool entered{};
        firstGraph.m_beforeImport = [&]
        {
            if (!entered)
            {
                entered = true;
                firstInside.count_down();
                releaseFirst.wait();
            }
        };
        std::exception_ptr firstError, secondError;
        std::jthread firstWorker(
            [&]
            {
                try
                {
                    value.frame->m_primitiveCache->Declare(firstGraph, visibility, value.frame);
                }
                catch (...)
                {
                    firstError = std::current_exception();
                }
            });
        firstInside.wait();
        std::jthread secondWorker(
            [&]
            {
                secondStarting.count_down();
                try
                {
                    second->m_primitiveCache->Declare(secondGraph, visibility, second);
                }
                catch (...)
                {
                    secondError = std::current_exception();
                }
            });
        secondStarting.wait();
        releaseFirst.count_down();
        firstWorker.join();
        secondWorker.join();
        require(!firstError && !secondError && firstGraph.m_passes.size() == 2 && secondGraph.m_passes.size() == 2,
                "Independent graph workers sharing retained scratch serialize without rejecting a legal declaration");

        fixture failed;
        failed.prepare(true);
        EnhancedRenderGraph partial;
        partial.m_failPass = 2;
        const auto cache = failed.frame->m_primitiveCache;
        requires_failure([&] { cache->Declare(partial, visibility, failed.frame); },
                         "Population AddPass failure propagates");
        require(partial.m_passes.size() == 1, "Failure fixture retains only the partial reset pass");
        partial.m_failPass = 0;
        requires_failure([&] { cache->Declare(partial, visibility, failed.frame); },
                         "A partial graph cannot be mistaken for an idempotently completed declaration");
        std::vector<EnhancedRenderGraph::RGPassUsage> reads;
        requires_failure([&] { cache->AddRead(partial, reads); },
                         "A failed population never publishes cache data to consumers");
        RHIEncoder encoder;
        requires_failure([&] { partial.m_passes[0].execute({&partial, &encoder}); },
                         "An incomplete cache cannot record retained partial callbacks");
    }
} // namespace

int main()
{
    try
    {
        test_disabled_and_permutation();
        test_graph_and_retirement();
        test_failures_and_limits();
        test_partial_graph_and_shared_scratch();
        std::cout << "PRIMITIVE_CACHE_LIFETIME_OK checks=" << checks
                  << " disabled_cache_allocations=0 native_gpu=false\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        count_allocations = false;
        std::cerr << "PRIMITIVE_CACHE_LIFETIME_FAILED " << error.what() << '\n';
        return 1;
    }
}
