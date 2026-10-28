#if defined(CE_CONTRACT_STANDALONE)
#include "../build/ModuleAdapters/Creator.Core.Ownership.h"
#else
import Creator.Core.Ownership;
#endif
#if defined(CE_CONTRACT_STANDALONE)
#include "../build/ModuleAdapters/Creator.Core.Paths.h"
#else
import Creator.Core.Paths;
#endif
#include "../../Engine/Utility_Framework/Foundation/StandardLibrary.h"
#if defined(_DEBUG)
#include <crtdbg.h>
#endif
#if defined(CE_CONTRACT_STANDALONE)
#include "../../Engine/Platform/WindowsSDK/Creator.ComSupport.h"
#else
import winrt_base;
#endif
#include "material_owner_checks.h"
#include "support/MaterialPipelineSlot.h"
#include "MaterialGraphRenderBindings.h"
#include "MaterialGraphTransientBufferPool.h"
#include "MaterialGraphPreparationTasks.h"
#include "Texture.h"
#include "TextureFramePins.h"
#include "RHI/DX12/DX12DeviceResources.h"
#include "RHI/DX12/DX12RootSignatureCache.h"
#include "RHI/DX12/DX12PSOManager.h"
#include "RHI/DX12/DX12TextureCache.h"

#include "../../Engine/Utility_Framework/Foundation/StandardLibrary.h"

namespace
{
    using namespace material_graph;
    using namespace LX;
    std::size_t checks{};
    unsigned poolAllocationChecks{};
    unsigned poolReleaseChecks{};
    unsigned nativeCacheAllocationChecks{};
    unsigned nativeCacheReleaseChecks{};
#if defined(_DEBUG)
    thread_local bool observePoolAllocations = false;
    thread_local unsigned poolAllocations = 0;
    thread_local unsigned poolFrees = 0;
    int PoolAllocationHook(int kind, void*, size_t, int, long, const unsigned char*, int)
    {
        if (observePoolAllocations && (kind == _HOOK_ALLOC || kind == _HOOK_REALLOC)) ++poolAllocations;
        if (observePoolAllocations && kind == _HOOK_FREE) ++poolFrees;
        return 1;
    }
    struct PoolAllocationScope
    {
        _CRT_ALLOC_HOOK previous;
        PoolAllocationScope() : previous(_CrtSetAllocHook(PoolAllocationHook))
        { poolAllocations = poolFrees = 0; observePoolAllocations = true; }
        ~PoolAllocationScope() { observePoolAllocations = false; _CrtSetAllocHook(previous); }
    };
#endif
    class ObservedPoolDevice final : public DX12DeviceResources
    {
      public:
#if defined(_DEBUG)
        bool observeNextRelease = false;
        unsigned freesBeforeNativeRelease = 0;
        void ReleaseBuffer(RHIBufferHandle handle) override
        {
            if (observeNextRelease)
            {
                freesBeforeNativeRelease = poolFrees;
                observeNextRelease = false;
                // Native bookkeeping and driver allocations are outside this check.
                observePoolAllocations = false;
            }
            DX12DeviceResources::ReleaseBuffer(handle);
        }
#endif
    };

    void Check(bool condition, const std::string& message)
    {
        ++checks;
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }

    class ObservedTextureCache final : public DX12TextureCache
    {
      public:
        std::size_t suppliedImages{}, residentProbes{};
        RHITextureEntry GetOrUpload(const Texture* texture, const own::shared_owner<const Texture::CodecImage>& image,
                                    std::string& error) override
        {
            suppliedImages += bool(image);
            residentProbes += !image;
            return DX12TextureCache::GetOrUpload(texture, image, error);
        }
    };

    void CheckBindlessTextureRegistry()
    {
        Check(sizeof(BindlessMaterialFrame) < 4096, "Empty frame excludes the maximum texture index storage");
        auto retained = std::make_shared<BindlessMaterialFrame>();
        Check(retained->textureSlots.empty(), "Empty texture frames allocate no key index");
        // Full capacity forces collision probing; existing keys remain valid at capacity.
        for (std::uint32_t i = 0; i < BindlessTextureCapacity; ++i)
        {
            const auto description = RHIBindingDesc::Srv2D(RHITextureHandle{i + 1}, RHIFormat::RGBA8Unorm);
            Check(retained->RegisterTexture(description) == i, "Bindless registry assigns stable insertion indices");
            if (i == 15)
            {
                Check(retained->textureSlots.size() < BindlessTextureCapacity / 2,
                      "Small descriptor sets keep a small index before later capacity growth");
            }
        }
        for (std::uint32_t i = 0; i < BindlessTextureCapacity; ++i)
        {
            const auto description = RHIBindingDesc::Srv2D(RHITextureHandle{i + 1}, RHIFormat::RGBA8Unorm);
            Check(retained->RegisterTexture(description) == i,
                  "Collision lookup preserves existing indices at capacity");
        }
        Check(retained->RegisterTexture(RHIBindingDesc::Srv2D(RHITextureHandle{1}, RHIFormat::RGBA8Unorm, 1)) ==
                  UINT32_MAX,
              "Distinct mip keys respect the full descriptor limit");
        Check(retained->descriptions.size() == BindlessTextureCapacity, "Rejected insertion preserves prior table");
        {
            auto next = std::make_shared<BindlessMaterialFrame>();
            const auto first = RHIBindingDesc::Srv2D(RHITextureHandle{2}, RHIFormat::RGBA8Unorm);
            Check(next->RegisterTexture(first) == 0, "New frame has an independent registry");
        }
        Check(retained->descriptions[1].resource.id == 2, "Retained frame survives subsequent registry retirement");
    }

    Id Pin(const LXGraph& graph, Id node, const std::string& name, Direction direction)
    {
        for (const auto& pin : graph.FindNode(node)->pins)
        {
            if (pin.Identifier() == name && pin.direction == direction)
            {
                return pin.id;
            }
        }
        throw std::runtime_error("Missing fixture pin: " + name);
    }

    void Connect(LXGraph& graph, Id from, const std::string& output, Id to, const std::string& input)
    {
        Check(graph.Connect(Pin(graph, from, output, Direction::Output), Pin(graph, to, input, Direction::Input))
                  .has_value(),
              "Fixture connection");
    }

    VerifiedProduct CompileFixture(const std::filesystem::path& root)
    {
        LXMaterialAsset asset;
        const auto surface = asset.CreateNode("ShaderNodeBsdfPrincipled", 0, 0);
        const auto output = asset.CreateNode("ShaderNodeOutputMaterial", 400, 0);
        const auto roughness = asset.CreateNode("LXParameterFloat", -100, 100);
        asset.activeOutput = output;
        asset.blackboard = {{900, "roughness", "Roughness", PinType::Float, .31}};
        Check(asset.graph.SetSocketValue(Pin(asset.graph, surface, "Emission Strength", Direction::Input), 1.0),
              "Keep both emission sample inputs active");
        Check(asset.graph.SetProperty(roughness, "parameter", "900"), "Fixture parameter");
        Connect(asset.graph, roughness, "Value", surface, "Roughness");
        Connect(asset.graph, surface, "BSDF", output, "Surface");
        for (std::size_t pair = 0; pair < 2; ++pair)
        {
            const auto multiply = asset.CreateNode("LXMultiplyColor", -200, 0);
            for (std::size_t side = 0; side < 2; ++side)
            {
                const auto image = asset.CreateNode("ShaderNodeTexImage", -400, 0);
                Check(asset.graph.SetProperty(image, "image", "22222222-2222-4222-8222-222222222222"), "Texture GUID");
                const std::string interpolation = side ? "Closest" : "Linear";
                const std::string extension = pair == side ? "REPEAT" : "EXTEND";
                asset.graph.SetProperty(image, "interpolation", interpolation);
                asset.graph.SetProperty(image, "extension", extension);
                Check(asset.graph.FindNode(image)->properties.at("interpolation") == interpolation, "Interpolation");
                Check(asset.graph.FindNode(image)->properties.at("extension") == extension, "Addressing");
                Connect(asset.graph, image, "Color", multiply, side ? "B" : "A");
            }
            Connect(asset.graph, multiply, "Result", surface, pair ? "Emission Color" : "Base Color");
        }
        ce::dynamic_array<LXMaterialDiagnostic> diagnostics;
        const auto generated = GenerateMaterialSlang(asset, &diagnostics);
        Check(!!generated, "Generate four independent samplers");
        const auto file = root / "Build/Obj/MaterialProductProbe/render-bindings.slang";
        std::ofstream source(file, std::ios::binary | std::ios::trunc);
        source << BuildBoundSource(*generated) << R"(
[shader("vertex")]
float4 VSMain(uint id : SV_VertexID) : SV_Position
{
    const float2 vertices[3] = {float2(-1, -1), float2(-1, 3), float2(3, -1)};
    return float4(vertices[id], 0, 1);
}
[shader("fragment")]
float4 PSMain(float4 position : SV_Position) : SV_Target
{
    const float coordinates[4] = {0.375, 1.25, -0.25, 0.75};
    LXMaterialContext context;
    context.uv = float3(coordinates[uint(position.x)], 0.5, 0);
    context.lod = 0;
    context.normal = float3(0, 0, 1);
    context.tangent = float3(1, 0, 0);
    context.bitangent = float3(0, 1, 0);
    MaterialInputs material = LXGenerateMaterial(context, LXBoundMaterialParameters()).surfaceInputs;
    return float4(material.baseColor.r, material.emissionColor.g, material.roughness, material.baseColor.b);
}
)";
        source.close();
        RHIShaderCompileOptions options;
        options.includeDirectories.push_back(root / "Dynamic_CPP/Assets/Shaders/DefaultPassShader/Includes");
        const CompileTarget targets[] = {{RHIShaderBinary::Dxil, "VSMain", "vs_6_0"},
                                         {RHIShaderBinary::Dxil, "PSMain", "ps_6_0"},
                                         {RHIShaderBinary::SpirV, "VSMain", "vs_6_0"},
                                         {RHIShaderBinary::SpirV, "PSMain", "ps_6_0"}};
        VerifiedProduct product;
        Check(VerifyProduct(*generated, file, targets, {}, options, {.coreForward = true}, {}, product, diagnostics),
              diagnostics.empty() ? "Verify product bindings" : diagnostics.front().message);
        Check(product.layout.textures.size() == 1 && product.layout.samplers.size() == 4,
              "One image, four sampler slots");
        return product;
    }

    float Decode(float encoded)
    {
        return encoded <= .04045f ? encoded / 12.92f : std::pow((encoded + .055f) / 1.055f, 2.4f);
    }

    float Sample(float u, std::size_t channel, bool linear, bool repeat)
    {
        const std::array<std::array<unsigned, 3>, 2> values{{{128, 64, 32}, {255, 192, 160}}};
        const auto texel = [&](int index)
        {
            index = repeat ? (index % 2 + 2) % 2 : std::clamp(index, 0, 1);
            return Decode(float(values[index][channel]) / 255.f);
        };
        if (!linear)
        {
            return texel(int(std::floor(u * 2.f)));
        }
        const float coordinate = u * 2.f - .5f;
        const int first = int(std::floor(coordinate));
        return std::lerp(texel(first), texel(first + 1), coordinate - float(first));
    }

    void Run(const std::filesystem::path& root)
    {
        auto* paths = (&ce::paths::get_state());
        paths->base_project_path = root / "Dynamic_CPP";
        paths->shader_source_path = root / "Dynamic_CPP/Assets/Shaders";
        paths->asset_authoring_enabled = true;
        const auto product = CompileFixture(root);
        {
            BindingLayout reflected;
            reflected.uniformBytes = 16;
            reflected.parameters.resize(1);
            reflected.textureEncodings.resize(1);
            reflected.textures.resize(1);
            reflected.samplers.resize(1);
            Check(reflected == BindingLayout(reflected), "Equal reflected layouts preserve exact value identity");
            Check(BindingLayout{} == BindingLayout{}, "Empty reflected layouts compare without storage");
            for (unsigned field = 0; field < 5; ++field)
            {
                auto changed = reflected;
                switch (field)
                {
                case 0:
                    ++changed.uniformBytes;
                    break;
                case 1:
                    ++changed.parameters[0].offset;
                    break;
                case 2:
                    ++changed.textureEncodings[0].offset;
                    break;
                case 3:
                    ++changed.textures[0].slot;
                    break;
                case 4:
                    ++changed.samplers[0].slot;
                    break;
                }
                Check(reflected != changed && changed != reflected, "Each reflected field rejects a changed layout");
            }
            auto shorter = reflected;
            shorter.parameters.clear();
            Check(reflected != shorter, "Reflected layout length mismatch is rejected");
        }
        GenerationStore store;
        experiment::AssetId graph;
        Check(uuid::try_parse("11111111-1111-4111-8111-111111111111", graph.value), "Graph identity");
        std::string error;
        const auto generation = store.Load(
            graph,
            [&](CookedProgram& cooked, std::string&)
            {
                cooked = {product, WriteMaterialProgramMetadata(product.program), BuildBoundSource(product.program)};
                return true;
            },
            false, error);
        Check(!!generation, "Owning verified generation: " + error);
        auto image = TextureImage::Allocate(RHIFormat::RGBA8UnormSrgb, 2, 1, 1, 1);
        const std::array<std::uint8_t, 8> pixels{128, 64, 32, 255, 255, 192, 160, 255};
        std::memcpy(image.MutablePixelsAt(*image.Find(0, 0)), pixels.data(), pixels.size());
        const auto texture = Texture::CreateSharedFromImage("METEOR.RenderBindings.SRGB", std::move(image));
        Check(!!texture, "Actual CPU texture");
        auto normalProduct = product;
        normalProduct.program.semanticKey += "|bc5-storage-binding-fixture";
        for (auto& resource : normalProduct.program.resources)
        {
            if (resource.kind == LXMaterialResourceKind::Texture)
            {
                resource.colorSpace = LXColorSpace::Data;
            }
        }
        for (auto& resource : normalProduct.layout.textures)
        {
            resource.colorSpace = LXColorSpace::Data;
        }
        experiment::AssetId normalGraph;
        Check(uuid::try_parse("33333333-3333-4333-8333-333333333333", normalGraph.value), "Normal graph identity");
        const auto normalGeneration = store.Load(
            normalGraph,
            [&](CookedProgram& cooked, std::string&)
            {
                cooked = {normalProduct, WriteMaterialProgramMetadata(normalProduct.program),
                          BuildBoundSource(normalProduct.program)};
                return true;
            },
            false, error);
        Check(!!normalGeneration, "Own graph generation for runtime BC5 binding: " + error);
        auto normalImage = TextureImage::Allocate(RHIFormat::BC5Unorm, 4, 4, 1, 1);
        auto* block = normalImage.MutablePixelsAt(*normalImage.Find(0, 0));
        std::memset(block, 0, 16);
        block[0] = block[1] = block[8] = block[9] = std::byte{128};
        const auto normalTexture = Texture::CreateSharedFromImage("METEOR.RenderBindings.BC5", std::move(normalImage));
        Check(!!normalTexture, "Actual CPU BC5 normal block");
        own::shared_owner<const Instance> normalInstance;
        Check(BuildInstance(
                  normalGeneration, {normalGraph, {{900, .31}}, {}},
                  [&](const experiment::AssetId&, LXColorSpace, std::string&) { return normalTexture; }, normalInstance,
                  error),
              "Build immutable instance with zero-default private encoding fields: " + error);
        own::shared_owner<const Instance> instance;
        Check(BuildInstance(
                  generation, {graph, {{900, .31}}, {}},
                  [&](const experiment::AssetId&, LXColorSpace, std::string&) { return texture; }, instance, error),
              "Build owning instance: " + error);

        ObservedPoolDevice device;
        Check(device.Initialize(4, 1, error), "Actual DX12 device: " + error);
        Check(device.HasDebugMessageQueue(), "D3D12 validation is installed");
        {
            TransientBufferPool pool(device);
            std::vector<RHIBufferHandle> handles(96);
            RHIBufferDesc desc;
            desc.bytes = 4096;
            desc.allowUnorderedAccess = true;
            desc.debugName = L"Pool regression initial";
            for (auto& handle : handles)
            {
                Check(pool.Acquire(desc, handle, error), "Pool initial acquire: " + error);
            }
            for (const auto handle : handles)
            {
                pool.Return(handle);
            }
            Check(pool.Snapshot().cachedCount == handles.size(), "Retain more than 64 small packet buffers");
            desc.bytes = 2048;
            desc.debugName = L"Pool regression renamed";
            for (std::size_t first = 0; first < handles.size(); first += 4)
            {
                const ce::fixed_array requests{desc, desc, desc, desc};
                ce::fixed_array<RHIBufferHandle, 4> acquired;
                Check(pool.AcquireBatch(requests, acquired, error), "Pool compatible batch acquire: " + error);
                for (std::size_t index = 0; index < acquired.size(); ++index)
                {
                    handles[first + index] = acquired[index];
                }
            }
            std::set<std::uint32_t> distinctHandles;
            for (const auto handle : handles)
            {
                distinctHandles.insert(handle.id);
            }
            Check(distinctHandles.size() == handles.size(), "Batch reservations never alias active owners");
            Check(pool.Snapshot().creates == 96 && pool.Snapshot().hits == 96,
                  "Renamed smaller requests reuse all returned allocations without creating");
            RHIBufferHandle active;
            Check(pool.Acquire(desc, active, error), "Pool active lifetime acquire: " + error);
            for (const auto handle : handles)
            {
                Check(handle.id != active.id, "Active allocations cannot be reused");
                pool.Return(handle);
            }
            pool.Return(active);
            desc.allowIndirectArguments = true;
            Check(pool.Acquire(desc, active, error), "Pool indirect acquire: " + error);
            Check(pool.Snapshot().creates == 98, "Indirect usage requires a compatible allocation");
            pool.Return(active);
            desc.allowIndirectArguments = false;
            desc.bytes = 8192;
            Check(pool.Acquire(desc, active, error), "Pool larger request: " + error);
            Check(pool.Snapshot().capacityMisses == 1, "Insufficient cached capacity is counted");
            const auto larger = active;
            pool.Return(larger);
            desc.bytes = 4096;
            Check(pool.Acquire(desc, active, error), "Pool best fit request: " + error);
            Check(active.id != larger.id, "Best fit preserves the larger allocation");
            pool.Return(active);
            desc.bytes = 65ull << 20;
            Check(pool.Acquire(desc, active, error), "Pool oversized allocation: " + error);
            pool.Return(active);
            Check(pool.Snapshot().budgetReleases == 1 && pool.Snapshot().cachedBytes <= (64ull << 20),
                  "Oversized allocations do not exceed the cache budget");
            desc.bytes = 4096;
            {
                const auto before = pool.Snapshot();
                ce::fixed_array requests{desc, desc, desc, desc};
                requests[1].initialState = RHIResourceState::IndirectArgument;
                ce::fixed_array<RHIBufferHandle, 4> acquired;
                Check(!pool.AcquireBatch(requests, acquired, error), "Reject incompatible native batch request");
                for (const auto handle : acquired)
                {
                    Check(!handle.IsValid(), "Failed batch does not expose a partial allocation");
                }
                Check(pool.Snapshot().cachedCount == before.cachedCount &&
                          pool.Snapshot().ownedCount == before.ownedCount,
                      "Failed batch returns every selected owner without leaking resources");
            }
            Check(pool.Acquire(desc, active, error), "Pool outstanding allocation at close: " + error);
            pool.Close();
            Check(pool.Snapshot().ownedCount == 1, "Close preserves an outstanding owner's allocation");
            pool.Return(active);
            Check(pool.Snapshot().ownedCount == 0 && pool.Snapshot().cachedBytes == 0,
                  "Pool close releases cached allocations");
            Check(!pool.Acquire(desc, active, error), "Closed pool rejects new resources");
        }

        {
            // Cross the first bitset word on both success and failed creation.
            TransientBufferPool pool(device);
            RHIBufferDesc desc;
            desc.bytes = 256;
            desc.debugName = L"Pool multiword regression";
            ce::fixed_array<RHIBufferDesc, 65> requests;
            requests.fill(desc);
            ce::fixed_array<RHIBufferHandle, 65> handles{};
            Check(pool.AcquireBatch(requests, handles, error), "Create 65 distinct pool owners: " + error);
            bool unique = true;
            for (std::size_t i = 0; i < handles.size(); ++i)
            {
                unique &= handles[i].IsValid();
                for (std::size_t j = 0; j < i; ++j) unique &= handles[i].id != handles[j].id;
            }
            Check(unique && pool.Snapshot().ownedCount == 65, "Bit 64 creates and tracks its own buffer");
            for (const auto handle : handles) pool.Return(handle);
            Check(pool.AcquireBatch(requests, handles, error), "Warm 65-buffer pool path");
            for (const auto handle : handles) pool.Return(handle);
#if defined(_DEBUG)
            unsigned allocated = 0;
            bool recycled = true;
            {
                PoolAllocationScope scope;
                for (unsigned iteration = 0; iteration < 100; ++iteration)
                {
                    recycled &= pool.AcquireBatch(requests, handles, error);
                    for (const auto handle : handles) pool.Return(handle);
                }
                allocated = poolAllocations;
            }
            Check(recycled && allocated == 0, "Warmed pool lookup/acquire/return allocates zero on its calling thread");
            ++poolAllocationChecks;
#endif
            Check(pool.Snapshot().creates == 65 && pool.Snapshot().cachedCount == 65,
                  "Warmed batch does not create or lose resource owners");
            Check(pool.AcquireBatch(requests, handles, error), "Keep owners outstanding at close");
            pool.Close();
            Check(pool.Snapshot().ownedCount == 65 && pool.Snapshot().cachedCount == 0,
                  "Close retains every outstanding owner");
            for (const auto handle : handles) pool.Return(handle);
            Check(pool.Snapshot().ownedCount == 0 && pool.Snapshot().releases == 65,
                  "Late returns release outstanding buffers exactly once");
        }
        {
            TransientBufferPool pool(device);
            RHIBufferDesc desc;
            desc.bytes = 256;
            ce::fixed_array<RHIBufferDesc, 65> requests;
            requests.fill(desc);
            // The final request is rejected by the native device, after 64 creations.
            requests[64].initialState = RHIResourceState::IndirectArgument;
            ce::fixed_array<RHIBufferHandle, 65> handles{};
            Check(!pool.AcquireBatch(requests, handles, error), "Reject creation at the second bitset word");
            bool hidden = true;
            for (const auto handle : handles) hidden &= !handle.IsValid();
            Check(hidden && pool.Snapshot().ownedCount == 64 && pool.Snapshot().cachedCount == 64,
                  "Failed batch hides all handles and returns created buffers to the cache");
            pool.Close();
            Check(pool.Snapshot().ownedCount == 0 && pool.Snapshot().releases == 64,
                  "Close releases cached owners from a failed batch exactly once");
        }

#if defined(_DEBUG)
        {
            TransientBufferPool pool(device);
            RHIBufferDesc desc;
            desc.bytes = 256;
            RHIBufferHandle handle;
            Check(pool.Acquire(desc, handle, error), "Create outstanding owner for node release check");
            pool.Close();
            unsigned observedFrees = 0;
            {
                PoolAllocationScope scope;
                device.observeNextRelease = true;
                pool.Return(handle);
                observedFrees = device.freesBeforeNativeRelease;
            }
            Check(!device.observeNextRelease && observedFrees == 1 && pool.Snapshot().ownedCount == 0,
                  "Closed return frees its tracking node before invoking native release");
            ++poolReleaseChecks;
        }
#endif
        {
            thread_pool workers;
            job_scheduler jobs(workers);
            jobs.start(2);
            std::array<unsigned, 4> values{};
            std::promise<void> entered, release;
            auto ready = entered.get_future();
            auto gate = release.get_future().share();
            {
                ScenePreparationTasks tasks(jobs, true);
                const bool admitted = tasks.Add(
                    ce::marker<"ScenePreparationRegression">(),
                    [&](std::string&)
                    {
                        entered.set_value();
                        gate.wait();
                        values[0] = 41;
                        return true;
                    },
                    error);
                const bool overlaps = admitted && ready.wait_for(std::chrono::seconds(5)) == std::future_status::ready;
                values[1] = 43;
                release.set_value();
                Check(admitted && overlaps, "Scene task runs before owner-side work joins");
                Check(tasks.Join(error) && values[0] == 41 && values[1] == 43,
                      "Scene task join publishes completed outputs");
                Check(tasks.Join(error), "Scene task join is idempotent");
            }
            {
                ScenePreparationTasks tasks(jobs, true);
                Check(tasks.Add(
                          ce::marker<"ScenePreparationRegression">(),
                          [](std::string& failure)
                          {
                              failure = "injected scene preparation failure";
                              return false;
                          },
                          error),
                      "Admit failing scene task");
                Check(tasks.Add(
                          ce::marker<"ScenePreparationRegression">(),
                          [&](std::string&)
                          {
                              values[2] = 47;
                              return true;
                          },
                          error),
                      "Admit independent scene task");
                Check(!tasks.Join(error) && error == "injected scene preparation failure" && values[2] == 47,
                      "Failure joins independent accepted work before returning");
            }
            {
                ScenePreparationTasks tasks(jobs, true);
                Check(tasks.Add(
                          ce::marker<"ScenePreparationRegression">(),
                          [&](std::string&)
                          {
                              values[3] = 53;
                              return true;
                          },
                          error),
                      "Admit early-return lifetime task");
            }
            Check(values[3] == 53, "Scope exit drains tasks before captured storage is destroyed");
            {
                ScenePreparationTasks tasks(jobs, false);
                const auto owner = std::this_thread::get_id();
                bool inlineOwner{};
                Check(tasks.Add(
                          ce::marker<"ScenePreparationRegression">(),
                          [&](std::string&)
                          {
                              inlineOwner = std::this_thread::get_id() == owner;
                              return true;
                          },
                          error) &&
                          inlineOwner && tasks.Join(error),
                      "Small preparation stays on its owner");
            }
            {
                // Setup may admit cascade leaves while the owner fills material data.
                // Drain the producer first, then every accepted child, on failure too.
                std::array<unsigned, 3> outputs{};
                ScenePreparationTasks cascades(jobs, true);
                ScenePreparationTasks setup(jobs, true);
                const bool admitted = setup.Add(
                    ce::marker<"ScenePreparationRegression">(),
                    [&](std::string& failure)
                    {
                        if (!cascades.Add(
                                ce::marker<"ScenePreparationRegression">(),
                                [&](std::string&)
                                {
                                    outputs[0] = 59;
                                    return true;
                                },
                                failure))
                        {
                            return false;
                        }
                        outputs[1] = 61;
                        failure = "injected caster setup failure after child admission";
                        return false;
                    },
                    error);
                outputs[2] = 67;
                Check(admitted, "Admit caster setup independently of owner material work");
                Check(!setup.Join(error) && outputs[1] == 61,
                      "Failed setup is drained before its child group is consumed");
                Check(cascades.Join(error) && outputs[0] == 59 && outputs[2] == 67,
                      "Accepted cascade survives setup failure and joins before snapshot destruction");
            }
            {
                std::promise<void> firstEntered, secondEntered, release, assisted;
                auto firstReady = firstEntered.get_future();
                auto secondReady = secondEntered.get_future();
                auto completed = assisted.get_future();
                const auto gate = release.get_future().share();
                std::once_flag releaseOnce;
                const auto unblock = [&] { std::call_once(releaseOnce, [&] { release.set_value(); }); };
                auto first = jobs.submit(
                    [&]
                    {
                        firstEntered.set_value();
                        gate.wait();
                    });
                auto second = jobs.submit(
                    [&]
                    {
                        secondEntered.set_value();
                        gate.wait();
                    });
                const bool occupied = firstReady.wait_for(std::chrono::seconds(5)) == std::future_status::ready &&
                                      secondReady.wait_for(std::chrono::seconds(5)) == std::future_status::ready;
                std::thread::id helperId, executorId;
                bool helperWorkerContext{};
                auto remaining = jobs.submit(
                    [&]
                    {
                        executorId = std::this_thread::get_id();
                        helperWorkerContext = thread_pool::is_worker_thread();
                        unblock();
                    });
                std::thread helper(
                    [&]
                    {
                        helperId = std::this_thread::get_id();
                        remaining.wait_assisting();
                        assisted.set_value();
                    });
                const bool helped = completed.wait_for(std::chrono::seconds(5)) == std::future_status::ready;
                unblock();
                helper.join();
                first.wait();
                second.wait();
                Check(occupied && helped && executorId == helperId && helperWorkerContext,
                      "External joining owner executes queued work while both native workers are occupied");
                Check(!thread_pool::is_worker_thread() && jobs.is_running(),
                      "Assistance deregisters the owner and restores its execution context");
            }
            jobs.shutdown();
            error.clear();
        }
        {
            // Actual COM owner, independent registrations. Readers retain their own
            // handles while other workers grow/reuse the registry concurrently.
            winrt::com_ptr<ID3D12Resource> resource;
            D3D12_HEAP_PROPERTIES heap{};
            heap.Type = D3D12_HEAP_TYPE_DEFAULT;
            D3D12_RESOURCE_DESC desc{};
            desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            desc.Width = desc.Height = desc.DepthOrArraySize = desc.MipLevels = 1;
            desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            desc.SampleDesc.Count = 1;
            Check(SUCCEEDED(device.GetDevice()->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                                                                        D3D12_RESOURCE_STATE_COMMON, nullptr,
                                                                        IID_PPV_ARGS(resource.put()))),
                  "Actual resource for concurrent texture registry");
            DX12ResourceTable registry;
            const auto pinned = registry.AddExternalTexture(resource.get());
            std::atomic<bool> valid{true};
            for (unsigned round = 0; round < 16; ++round)
            {
                std::array<std::array<RHITextureHandle, 256>, 4> handles{};
                std::array<std::thread, 4> workers;
                std::barrier gate(4);
                for (std::size_t worker = 0; worker < workers.size(); ++worker)
                {
                    workers[worker] = std::thread(
                        [&, worker]
                        {
                            gate.arrive_and_wait();
                            for (auto& handle : handles[worker])
                            {
                                handle = registry.AddTexture(resource);
                                if (!handle.IsValid() || registry.Resolve(handle) != resource.get() ||
                                    registry.Resolve(pinned) != resource.get())
                                {
                                    valid.store(false, std::memory_order_relaxed);
                                }
                            }
                            gate.arrive_and_wait();
                            for (const auto handle : handles[worker])
                            {
                                registry.Release(handle);
                            }
                        });
                }
                for (auto& worker : workers)
                {
                    worker.join();
                }
                std::set<std::uint32_t> unique;
                for (const auto& row : handles)
                {
                    for (const auto handle : row)
                    {
                        unique.insert(handle.id);
                    }
                }
                Check(valid.load() && unique.size() == 1024 && registry.LiveTextureCount() == 1 &&
                          !registry.Resolve(handles[0][0]),
                      "Concurrent texture slots retain owners, unique generations and stable lookup");
            }
            registry.Clear();
            Check(registry.LiveTextureCount() == 0 && !registry.Resolve(pinned) && resource->GetDesc().Width == 1,
                  "Texture registry clear drains owned slots and preserves external resource lifetime");
        }
        {
            DX12RootSignatureCache cache;
            DX12SamplerHeap samplers;
            Check(cache.Initialize(&device, error), "Isolated native root cache");
            Check(samplers.Initialize(device.GetDevice(), 8, error), "Isolated native sampler cache");
            const RHIPipelineLayoutParam params[] = {RHILayout::Cbv(0)};
            const RHIPipelineLayoutDesc desc{params, {}, false};
            const auto root = cache.GetOrCreate(desc, error);
            const auto sampler = samplers.GetOrCreate(RHISampler::Point());
            Check(root.IsValid() && sampler.ptr != 0, "Native cache initial owners");
            // Keep native objects alive outside the shutdown measurement. Driver
            // final-release activity must not be counted as tracking-node frees.
            winrt::com_ptr<ID3D12RootSignature> retainedRoot;
            retainedRoot.copy_from(device.Resolve(root).signature);
            winrt::com_ptr<ID3D12DescriptorHeap> retainedHeap;
            retainedHeap.copy_from(samplers.GetHeap());
            bool same = true;
#if defined(_DEBUG)
            unsigned allocations = 0;
            {
                PoolAllocationScope scope;
#endif
                for (unsigned iteration = 0; iteration < 4096; ++iteration)
                {
                    same &= cache.GetOrCreate(desc, error).id == root.id;
                    same &= samplers.GetOrCreate(RHISampler::Point()).ptr == sampler.ptr;
                }
#if defined(_DEBUG)
                allocations = poolAllocations;
            }
            Check(allocations == 0, "Warmed root/sampler cache lookup allocates zero on calling thread");
            ++nativeCacheAllocationChecks;
#endif
            Check(same && cache.GetCachedCount() == 1 && samplers.GetCachedCount() == 1 &&
                      cache.GetStats().creates == 1 && samplers.GetStats().creates == 1,
                  "Cache hits preserve native identity and create counts");
#if defined(_DEBUG)
            unsigned rootFrees = 0, samplerFrees = 0;
            {
                PoolAllocationScope scope;
                cache.Shutdown();
                rootFrees = poolFrees;
            }
            {
                PoolAllocationScope scope;
                samplers.Shutdown();
                samplerFrees = poolFrees;
            }
            Check(rootFrees == 1 && samplerFrees == 1,
                  "Each native cache clear immediately frees its one tracking node");
            ++nativeCacheReleaseChecks;
#else
            cache.Shutdown();
            samplers.Shutdown();
#endif
            Check(cache.GetCachedCount() == 0 && samplers.GetCachedCount() == 0 &&
                      !cache.IsInitialized() && !samplers.IsInitialized(), "Shutdown empties native caches");
            winrt::com_ptr<ID3D12Device> retainedDevice;
            Check(SUCCEEDED(retainedRoot->GetDevice(IID_PPV_ARGS(retainedDevice.put()))) &&
                      retainedHeap->GetDesc().NumDescriptors == 8,
                  "External COM owners survive cache shutdown");
            // No use of the non-owning root handle or sampler handle after shutdown.
        }
        DX12RootSignatureCache roots;
        DX12PSOManager pipelines;
        ObservedTextureCache textures;
        Check(roots.Initialize(&device, error), "Actual root signature cache");
        Check(pipelines.Initialize(&device, L"", error), "Actual pipeline cache: " + error);
        Check(textures.Initialize(&device, error), "Actual texture cache: " + error);
        PassLayout layout;
        const RHIPipelineLayoutParam host[] = {RHILayout::Cbv(0)};
        Check(CreatePassLayout(roots, product.layout, host, {}, false, layout, error),
              "Append independent material root ranges");
        Check(layout.uniformSlot == 1u && layout.textureSlot == 2u && layout.samplerSlot == 3u &&
                  layout.descriptorIndexSlot == 4u,
              "Exact appended root slots");
        PassLayout normalLayout;
        Check(CreatePassLayout(roots, normalProduct.layout, host, {}, false, normalLayout, error),
              "Create BC5 graph pass layout");
        const auto acceptedLayout = layout.handle;
        for (const auto conflict :
             {RHILayout::Cbv(2), RHILayout::Constants(2, 1), RHILayout::Cbv(DescriptorIndexRegister),
              RHILayout::Srv(TextureRegister), RHILayout::SrvTable(3, TextureRegister - 1),
              RHILayout::Srv(TextureRegister + BindlessTextureCapacity - 1), RHILayout::SamplerTable(5, 1)})
        {
            Check(!CreatePassLayout(roots, product.layout, {&conflict, 1}, {}, false, layout, error) &&
                      layout.handle == acceptedLayout,
                  "Conflicting host range preserves accepted layout");
        }
        const RHIStaticSamplerDesc staticConflict{RHISampler::Point(), 4};
        Check(!CreatePassLayout(roots, product.layout, {}, {&staticConflict, 1}, false, layout, error) &&
                  layout.handle == acceptedLayout,
              "Static sampler overlap rejection");
        auto invalidLayout = product.layout;
        invalidLayout.samplers.front().slot = 64;
        Check(!CreatePassLayout(roots, invalidLayout, {}, {}, false, layout, error) && layout.handle == acceptedLayout,
              "Out-of-range sampler slot retains accepted layout");
        invalidLayout = product.layout;
        invalidLayout.samplers.push_back(invalidLayout.samplers.front());
        Check(!CreatePassLayout(roots, invalidLayout, {}, {}, false, layout, error) && layout.handle == acceptedLayout,
              "Duplicate sampler slot retains accepted layout");

        RHITextureHandle target;
        RHITextureDesc targetDescription;
        targetDescription.width = 4;
        targetDescription.height = 1;
        targetDescription.format = RHIFormat::RGBA32Float;
        targetDescription.allowRenderTarget = true;
        targetDescription.debugName = L"METEOR.MaterialBinding.Target";
        Check(device.CreateTexture(targetDescription, target, error), "Actual render target");
        RHIReadback readback;
        Check(device.CreateReadback(4, 1, targetDescription.format, 1, readback, error), "Actual readback");
        RenderBindingCache bindings;
        own::shared_owner<const RenderBindings> packet;
        Check(!bindings.Prepare(device, textures, instance, layout, packet, error) && !packet,
              "No upload outside recording");
        PipelineSlot pipeline;
        std::size_t gpuComponents = 0;
        RHIResourceState targetState = RHIResourceState::Common;
        auto sharedLayout = own::make_shared<const PassLayout>(layout);
        own::weak_owner<const PassLayout> weakLayout(sharedLayout);
        own::shared_owner<const ResourcePacket> immutableNormalResources;
        for (unsigned frame = 0; frame < 4; ++frame)
        {
            Check(device.BeginFrame(error), "Actual BeginFrame");
            textures.BeginFrame(frame);
            auto& encoder = device.GetImmediateEncoder();
            {
                own::shared_owner<const RenderBindings> normalPacket;
                const bool allResident = std::ranges::all_of(std::span(normalInstance->textures), [&](const auto& value)
                                                             { return textures.IsResident(&*value.owner); });
                const auto priorImages = textures.suppliedImages;
                const auto priorProbes = textures.residentProbes;
                Check(bindings.Prepare(device, textures, normalInstance, normalLayout, normalPacket, error),
                      "Normal resource snapshot prepares across recordings: " + error);
                Check(allResident ? textures.suppliedImages == priorImages : textures.suppliedImages > priorImages,
                      "Cold upload resolves pixels; later recordings use resident entries without CPU image pins");
                Check(textures.residentProbes > priorProbes,
                      "Each recording refreshes native texture registration through the resident lookup");
                if (!immutableNormalResources)
                {
                    immutableNormalResources = normalPacket->resourceOwner;
                }
                Check(material_graph_test::SamePinnedObject(normalPacket->resourceOwner, immutableNormalResources) &&
                          normalPacket->resources.uniforms.data() == immutableNormalResources->uniforms.data(),
                      "New recordings share immutable uniform storage without ResourcePacket vector copies");
            }
            if (frame == 0)
            {
                own::shared_owner<const RenderBindings> normalPacket;
                Check(bindings.Prepare(device, textures, normalInstance, normalLayout, normalPacket, error),
                      "BC5 render-binding cache accepts authoritative runtime encoding uniforms: " + error);
                const auto offset = normalProduct.layout.textureEncodings.front().offset;
                std::uint32_t encoded{}, defaultEncoding{};
                std::memcpy(&encoded, normalPacket->resources.uniforms.data() + offset, sizeof(encoded));
                std::memcpy(&defaultEncoding, normalInstance->uniforms.data() + offset, sizeof(defaultEncoding));
                Check(encoded == static_cast<std::uint32_t>(TextureSampleEncoding::BC5Normal) && defaultEncoding == 0 &&
                          !std::ranges::equal(normalPacket->resources.uniforms, normalInstance->uniforms) &&
                          RenderBindingCache::ValidatePass(device, *normalPacket, normalLayout, error),
                      "Runtime BC5 metadata may differ while immutable numeric properties remain sealed");
                Instance corruptValue(*normalInstance);
                corruptValue.uniforms[normalProduct.layout.parameters.front().offset] ^= 1;
                const auto corrupt = own::make_shared<const Instance>(std::move(corruptValue));
                auto retained = normalPacket;
                Check(!bindings.Prepare(device, textures, corrupt, normalLayout, normalPacket, error) &&
                          material_graph_test::SamePinnedObject(normalPacket, retained),
                      "Encoding exceptions cannot hide corruption of immutable numeric parameter bytes");
            }
            if (packet)
            {
                Check(!RenderBindingCache::Bind(device, encoder, RHIBindPoint::Graphics, *packet, error),
                      "Reject stale-frame packet");
                Check(!RenderBindingCache::ValidatePass(device, *packet, layout, error),
                      "Pass binding rejects stale recordings");
            }
            // A prepared lookup is weak: after the previous recording's callers
            // release their packet, it cannot keep the CPU wrapper resident.
            own::weak_owner<const RenderBindings> previousPacket(packet);
            packet.reset();
            Check(previousPacket.expired(), "Prepared weak lookup does not own an unused binding packet");
            const double roughness = frame % 2 ? .81 : .31;
            Check(BuildInstance(
                      generation, {graph, {{900, roughness}}, {}},
                      [&](const experiment::AssetId&, LXColorSpace, std::string&) { return texture; }, instance, error),
                  "New immutable instance values");
            Check(bindings.Prepare(device, textures, instance, *sharedLayout, packet, error, {}, nullptr, sharedLayout),
                  "Actual RHI material prepare: " + error);
            Check(material_graph_test::SamePinnedObject(packet->layout, sharedLayout), "New recordings retain the exact immutable program layout snapshot");
            auto forgedLayout = own::make_shared<PassLayout>(layout);
            ++forgedLayout->hostParameterCount;
            const auto snapshotAccepted = packet;
            Check(!bindings.Prepare(device, textures, instance, layout, packet, error, {}, nullptr, forgedLayout) &&
                      material_graph_test::SamePinnedObject(packet, snapshotAccepted),
                  "Mismatched layout snapshot cannot bypass same-recording packet validation");
            Check(bindings.SamplerTableCount() == 1 && packet->resources.samplers.size() == 4,
                  "Sampler table is shared across repeated frames and value edits");
            const auto accepted = packet;
            own::shared_owner<const RenderBindings> repeated;
            Check(bindings.Prepare(device, textures, instance, layout, repeated, error) &&
                      material_graph_test::SamePinnedObject(repeated, packet),
                  "Same immutable instance and complete layout reuse one packet in this recording");
            Check(packet->instancePins && packet->instancePinIndex != InstanceFramePins::InvalidIndex &&
                      material_graph_test::SamePinnedObject(packet->instance, instance) &&
                      material_graph_test::SamePinnedObject(packet->instancePins->Borrow(packet->instancePinIndex),
                                                            instance) &&
                      packet->resources.owners.empty(),
                  "Binding packet borrows the exact instance and texture closure through its frame pin index");
            auto otherViewPins = own::make_shared<InstanceFramePins>();
            otherViewPins->Retain(instance);
            Check(bindings.Prepare(device, textures, instance, layout, repeated, error, otherViewPins) &&
                      material_graph_test::SamePinnedObject(repeated, packet) &&
                      !material_graph_test::SamePinnedObject(repeated->instancePins, otherViewPins),
                  "Same-recording reuse retains its original independent frame pin table");
            Check(RenderBindingCache::ValidatePass(device, *packet, layout, error),
                  "Second pass validates the shared material resources without copying");
            auto wrongLayout = layout;
            wrongLayout.samplerSlot.reset();
            Check(!RenderBindingCache::ValidatePass(device, *packet, wrongLayout, error),
                  "Pass binding rejects a missing reflected slot");
            Check(!bindings.Prepare(device, textures, instance, wrongLayout, packet, error) &&
                      material_graph_test::SamePinnedObject(packet, accepted),
                  "Missing sampler root retains accepted render packet");
            wrongLayout = layout;
            wrongLayout.descriptorIndexSlot.reset();
            Check(!RenderBindingCache::ValidatePass(device, *packet, wrongLayout, error),
                  "Pass binding rejects a missing bindless descriptor-index root");
            wrongLayout = layout;
            wrongLayout.samplerSlot = layout.uniformSlot;
            Check(!bindings.Prepare(device, textures, instance, wrongLayout, packet, error) &&
                      material_graph_test::SamePinnedObject(packet, accepted),
                  "Aliased root slots retain accepted render packet");
            Instance wrongInstanceValue(*instance);
            wrongInstanceValue.uniforms.clear();
            const auto wrongInstance = own::make_shared<const Instance>(std::move(wrongInstanceValue));
            Check(!bindings.Prepare(device, textures, wrongInstance, layout, packet, error, accepted->instancePins) &&
                      material_graph_test::SamePinnedObject(packet, accepted),
                  "Copied representation identity cannot substitute a different object in the frame pin table");
            Check(!bindings.Prepare(device, textures, wrongInstance, layout, packet, error) &&
                      material_graph_test::SamePinnedObject(packet, accepted),
                  "Invalid packed values retain accepted render packet");
            if (frame == 0)
            {
                Instance missingPixelsValue(*instance);
                missingPixelsValue.textures.front().owner = own::make_shared<const Texture>();
                const auto missingPixels = own::make_shared<const Instance>(std::move(missingPixelsValue));
                Check(!bindings.Prepare(device, textures, missingPixels, layout, packet, error) &&
                          material_graph_test::SamePinnedObject(packet, accepted) &&
                          textures.GetUploadFailureCount() == 0,
                      "Missing image preparation retains the accepted packet without an upload failure");
                TextureFramePins invalidImagePins;
                invalidImagePins.RetainImage(missingPixels->textures.front().owner, texture->NonRehydratableImage());
                Check(
                    !bindings.Prepare(device, textures, missingPixels, layout, packet, error, {}, &invalidImagePins) &&
                        material_graph_test::SamePinnedObject(packet, accepted) && !error.empty() &&
                        textures.GetUploadFailureCount() == 1,
                    "Actual cache neutral substitution is rejected and retains the accepted packet");
            }
            RHIGraphicsPipelineDesc description;
            description.layout = layout.handle;
            description.rtvFormats[0] = targetDescription.format;
            std::vector<TextureBinding> uploaded;
            for (const auto& owner : instance->textures)
            {
                uploaded.push_back({owner.slot,
                                    textures.GetOrUpload((owner.owner ? &*owner.owner.borrow() : nullptr),
                                                         owner.owner ? owner.owner->NonRehydratableImage()
                                                                     : own::shared_owner<const Texture::CodecImage>{},
                                                         error),
                                    owner.owner});
            }
            ce::dynamic_array<LXMaterialDiagnostic> diagnostics;
            Check(pipeline.Publish(pipelines, product, RHIShaderBinary::Dxil, {.coreForward = true}, {},
                                   instance->description.parameters, uploaded, description, {}, diagnostics),
                  "Actual PSO publication");
            encoder.SetPipeline(RHIBindPoint::Graphics, pipeline.Active()->pipeline.GetHandle());
            const std::array<float, 4> dummy{};
            encoder.SetConstantBuffer(RHIBindPoint::Graphics, 0, device.UploadConstants(dummy.data(), sizeof(dummy)));
            Check(RenderBindingCache::Bind(device, encoder, RHIBindPoint::Graphics, *packet, error),
                  "Actual material root binding");
            const RHITransition before{target, targetState, RHIResourceState::RenderTarget};
            encoder.ResourceBarriers({{&before, 1}});
            const auto renderTarget = device.CreateRenderTargets(std::span(&target, 1));
            Check(renderTarget.IsValid(), "Actual RTV descriptors");
            encoder.BindRenderTargets(renderTarget);
            encoder.SetViewportAndScissor(4, 1);
            encoder.SetPrimitiveTopology(RHIPrimitiveTopology::TriangleList);
            encoder.Draw(3, 1);
            const RHITransition after{target, RHIResourceState::RenderTarget, RHIResourceState::CopySource};
            encoder.ResourceBarriers({{&after, 1}});
            targetState = RHIResourceState::CopySource;
            encoder.CopyToReadback(readback, target);
            Check(device.EndFrame(error), "Actual submit: " + error);
            device.WaitForGpu();
            RHIReadbackImage values;
            Check(device.MapReadback(readback, values, error), "Actual GPU readback");
            const std::array<float, 4> coordinates{.375f, 1.25f, -.25f, .75f};
            for (std::uint32_t x = 0; x < 4; ++x)
            {
                const float u = coordinates[x];
                const std::array<float, 4> expected{Sample(u, 0, true, true) * Sample(u, 0, false, false),
                                                    Sample(u, 1, true, false) * Sample(u, 1, false, true),
                                                    float(roughness),
                                                    Sample(u, 2, true, true) * Sample(u, 2, false, false)};
                for (std::uint32_t channel = 0; channel < 4; ++channel)
                {
                    const float actual = values.At(x, 0, channel);
                    Check(std::isfinite(actual) && std::abs(actual - expected[channel]) < .001f,
                          "Actual sampler/address/SRGB/uniform GPU result: pixel=" + std::to_string(x) +
                              " channel=" + std::to_string(channel) + " actual=" + std::to_string(actual) +
                              " expected=" + std::to_string(expected[channel]));
                    ++gpuComponents;
                }
            }
            std::string messages;
            Check(device.DrainDebugMessages(messages) == 0, "D3D12 validation: " + messages);
        }
        Check(textures.GetUploadFailureCount() == 1 && textures.GetStats().uploads == 2,
              "Only the injected missing-pixels upload failed; SRGB and BC5 textures each upload once: failures=" +
                  std::to_string(textures.GetUploadFailureCount()) +
                  " uploads=" + std::to_string(textures.GetStats().uploads));
        device.ReleaseReadback(readback);
        device.ReleaseTexture(target);
        sharedLayout.reset();
        Check(!weakLayout.expired() && packet->layout && *packet->layout == layout,
              "Frame packet retains immutable layout after program owner release");
        packet.reset();
        Check(weakLayout.expired(), "Retired packet releases the last immutable layout owner");
        const auto retainedUniforms = immutableNormalResources->uniforms;
        bindings.Clear();
        Check(immutableNormalResources->uniforms == retainedUniforms,
              "Immutable resource storage survives description-cache eviction");
        textures.Shutdown();
        pipelines.Shutdown();
        roots.Shutdown();
        device.Shutdown();
        std::cout << "LX_MATERIAL_RENDER_BINDINGS_OK checks=" << checks << " gpuComponents=" << gpuComponents
                  << " samplerSlots=4 samplerTables=1 frames=4 poolAllocationChecks=" << poolAllocationChecks
                  << " poolReleaseChecks=" << poolReleaseChecks
                  << " nativeCacheAllocationChecks=" << nativeCacheAllocationChecks
                  << " nativeCacheReleaseChecks=" << nativeCacheReleaseChecks << "\n";
    }
    void CheckSamplerGrammar()
    {
        using namespace material_graph;
        const std::array<std::string_view, 2> filters{"nearest", "linear"};
        const std::array<std::string_view, 3> addresses{"repeat", "clamp", "mirror"};
        const std::array<RHIAddressMode, 3> modes{RHIAddressMode::Wrap, RHIAddressMode::Clamp, RHIAddressMode::Mirror};
        BindingLayout layout;
        layout.samplers.resize(1);
        ResourcePacket packet;
        ce::dynamic_array<LX::LXMaterialDiagnostic> diagnostics;
        for (std::size_t minMag = 0; minMag < filters.size(); ++minMag)
        {
            for (std::size_t mip = 0; mip < filters.size(); ++mip)
            {
                for (std::size_t u = 0; u < addresses.size(); ++u)
                {
                    for (std::size_t v = 0; v < addresses.size(); ++v)
                    {
                        const auto reference = std::string(filters[minMag]) + "-" + std::string(filters[mip]) + "-" +
                                               std::string(addresses[u]) + "-" + std::string(addresses[v]);
                        layout.samplers[0].reference = reference;
                        Check(PrepareResourcesWithUniforms(layout, {}, {}, packet, diagnostics) &&
                                  packet.samplers.size() == 1 &&
                                  packet.samplers[0].minMag ==
                                      (minMag ? RHIFilterMode::Linear : RHIFilterMode::Point) &&
                                  packet.samplers[0].mip == (mip ? RHIFilterMode::Linear : RHIFilterMode::Point) &&
                                  packet.samplers[0].addressU == modes[u] && packet.samplers[0].addressW == modes[u] &&
                                  packet.samplers[0].addressV == modes[v],
                              "Sampler grammar preserves all 36 cooked combinations");
                    }
                }
            }
        }
        for (const auto reference : {"linear-repeat", "linear-repeat-", "linear-linear-repeat-repeat-"})
        {
            layout.samplers[0].reference = reference;
            Check(PrepareResourcesWithUniforms(layout, {}, {}, packet, diagnostics) &&
                      packet.samplers[0].minMag == RHIFilterMode::Linear &&
                      packet.samplers[0].mip == RHIFilterMode::Linear &&
                      packet.samplers[0].addressU == RHIAddressMode::Wrap &&
                      packet.samplers[0].addressV == RHIAddressMode::Wrap,
                  "Sampler grammar preserves legacy pairs and trailing delimiter behavior");
        }
        for (const auto reference :
             {"", "linear", "linear-repeat-repeat", "linear--repeat-repeat", "linear-linear-repeat-repeat-extra",
              "linear-linear-repeat-repeat--", "linear-linear-repeat-repeat-extra-tail", "linear-linear-repeat-border"})
        {
            layout.samplers[0].reference = reference;
            Check(!PrepareResourcesWithUniforms(layout, {}, {}, packet, diagnostics) && packet.samplers.size() == 1 &&
                      packet.samplers[0].minMag == RHIFilterMode::Linear &&
                      packet.samplers[0].addressU == RHIAddressMode::Wrap,
                  "Malformed sampler references reject transactionally without replacing accepted state");
        }
    }

} // namespace

int main(int argc, char** argv)
{
    try
    {
        if (argc != 2)
        {
            throw std::runtime_error("Expected repository root.");
        }
        CheckBindlessTextureRegistry();
        CheckSamplerGrammar();
        Run(std::filesystem::absolute(argv[1]));
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "LX_MATERIAL_RENDER_BINDINGS_FAILED " << error.what() << '\n';
        return 1;
    }
}
