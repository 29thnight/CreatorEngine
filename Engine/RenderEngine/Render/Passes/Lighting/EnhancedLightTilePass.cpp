#include "EnhancedLightTilePass.h"
#include "../../Graph/EnhancedRenderGraph.h"
#include "../../../RHI/RHIEncoder.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace
{
    // One thread group per tile (16x16). The group gathers the tile's depth
    // range, builds the tile frustum in view space and tests light spheres.
    // Sky-only tiles receive no lights.
    constexpr const char* kCullShaderFile = "ForwardCull.slang";

    struct CullParams
    {
        math::matrix4x4 view{};
        math::matrix4x4 inverseProjection{};
        uint32_t screenWidth{0};
        uint32_t screenHeight{0};
        uint32_t tileGridX{0};
        uint32_t tileGridY{0};
        uint32_t lightCount{0};
        uint32_t pad[3]{};
    };
    static_assert(sizeof(CullParams) == 160u);
    static_assert(std::is_trivially_copyable_v<CullParams>);
}

bool EnhancedLightTilePass::Initialize(const EnhancedFrameContext& context, std::string& outError)
{
    if (!context.resources || !context.psoManager || !context.rootSignatures)
    {
        outError = "Light tiles require device, pipeline and layout services.";
        return false;
    }
    // b0 constants, t0 depth (table), t1 lights (root SRV), u0/u1 tile buffers (table).
    const RHIPipelineLayoutParam params[] = {
        RHILayout::Cbv(0),
        RHILayout::SrvTable(1, 0),
        RHILayout::Srv(1),
        RHILayout::UavBufferTable(2, 0),
    };
    RHIPipelineLayoutDesc rootDesc{};
    rootDesc.params = params;
    const auto root = context.rootSignatures->GetOrCreate(rootDesc, outError);
    if (!root.IsValid()) return false;

    RHIShaderPermutation permutation;
    if (!permutation.Set("TILE_SIZE", std::to_string(kTileSize), outError) ||
        !permutation.Set("MAX_LIGHTS_PER_TILE", std::to_string(kMaxLightsPerTile), outError))
        return false;
    LX::Runtime::CompiledCompute blob;
    if (!LX::Runtime::CompileCompute(kCullShaderFile, "CSMain", permutation, {}, blob, outError, "cs_5_0"))
        return false;

    RHIComputePipelineDesc desc{};
    desc.csBytecode = blob.stage.bytecode.Data();
    desc.csSize = blob.stage.bytecode.Size();
    desc.layout = root;
    LX::Runtime::ComputePipeline candidate;
    if (!candidate.Create(*context.psoManager, desc, std::move(blob.description), outError)) return false;
    m_cullPSO = std::move(candidate);
    return true;
}

bool EnhancedLightTilePass::EnsureTileBuffers(const EnhancedFrameContext& context, std::string& outError)
{
    if (!context.resources || !m_resourceRetirement.Attach(*context.resources))
    {
        outError = "Light tile buffers require their owning device.";
        return false;
    }
    const uint32_t tileTotal = m_tileCountX * m_tileCountY;
    if (0 == tileTotal) return true;
    if (m_tileCountBuffer.IsValid() && m_tileListBuffer.IsValid() && m_allocatedTiles >= tileTotal) return true;

    RHIBufferDesc desc{};
    desc.allowUnorderedAccess = true;
    // Twice the tiles: clamped counts read by shading, then the unclamped
    // counts so tile overflow stays measurable.
    desc.bytes = static_cast<uint64_t>(tileTotal) * 2ull * sizeof(uint32_t);
    desc.debugName = L"METEOR.Scene.LightTileCount";
    RHIBufferHandle count{}, list{};
    if (!context.resources->CreateBuffer(desc, count, outError))
    {
        if (count.IsValid()) context.resources->ReleaseBuffer(count);
        outError = "Light tile count buffer: " + outError;
        return false;
    }
    desc.bytes = static_cast<uint64_t>(tileTotal) * kMaxLightsPerTile * sizeof(uint32_t);
    desc.debugName = L"METEOR.Scene.LightTileList";
    if (!context.resources->CreateBuffer(desc, list, outError))
    {
        context.resources->ReleaseBuffer(count);
        if (list.IsValid()) context.resources->ReleaseBuffer(list);
        outError = "Light tile list buffer: " + outError;
        return false;
    }
    m_resourceRetirement.Retire(m_tileCountBuffer);
    m_resourceRetirement.Retire(m_tileListBuffer);
    m_tileCountBuffer = count;
    m_tileListBuffer = list;
    m_allocatedTiles = tileTotal;
    // New buffers start in Common; a stale end state would emit a wrong first barrier.
    m_tileCountState = RHIResourceState::Common;
    m_tileListState = RHIResourceState::Common;
    return true;
}

bool EnhancedLightTilePass::PrepareFrame(const EnhancedFrameContext& context, std::string& outError)
{
    m_tileCountX = (context.width + kTileSize - 1) / kTileSize;
    m_tileCountY = (context.height + kTileSize - 1) / kTileSize;
    m_lighting = {};
    if (!context.forwardLightingConsumer)
    {
        outError.clear();
        return true;
    }
    return EnsureTileBuffers(context, outError);
}

void EnhancedLightTilePass::Declare(EnhancedRenderGraph& graph, const EnhancedFrameContext& context)
{
    m_lighting = {};
    if (!context.forwardLightingConsumer || !m_depth.IsValid() || !m_cullPSO.IsValid() ||
        !m_tileCountBuffer.IsValid() || nullptr == context.lights)
    {
        return;
    }
    const bool versioned = graph.GetSchedulingMode() == RGSchedulingMode::ExplicitVersioned;
    const bool explicitAccess = graph.GetSchedulingMode() != RGSchedulingMode::DeclarationOrder;
    const auto readAccess = explicitAccess ? RGAccessMode::Read : RGAccessMode::LegacyState;
    const auto writeAccess = explicitAccess ? RGAccessMode::Write : RGAccessMode::LegacyState;

    auto counts = graph.ImportBuffer(m_tileCountBuffer, m_tileCountState, "METEOR.Scene.LightTileCount", &m_tileCountState);
    auto indices = graph.ImportBuffer(m_tileListBuffer, m_tileListState, "METEOR.Scene.LightTileList", &m_tileListState);
    if (versioned)
    {
        counts = graph.Write(counts);
        indices = graph.Write(indices);
    }

    const auto lightCount = static_cast<std::uint32_t>(context.lights->size());
    auto lights = context.resources->AllocateUpload(
        {(std::max)(std::uint64_t{1}, std::uint64_t{lightCount}) * sizeof(EnhancedLight), RHIUploadUsage::BufferCopy, 16});
    if (!lights.IsValid())
    {
        throw std::runtime_error("Light tile light upload failed.");
    }
    if (lightCount)
    {
        std::memcpy(lights.cpuAddress, context.lights->data(), lightCount * sizeof(EnhancedLight));
    }

    CullParams params{};
    if (context.camera)
    {
        params.view = math::transpose(context.camera->view);
        params.inverseProjection = math::transpose(context.camera->inverseProjection);
    }
    params.screenWidth = context.width;
    params.screenHeight = context.height;
    params.tileGridX = m_tileCountX;
    params.tileGridY = m_tileCountY;
    params.lightCount = lightCount;
    const auto constants = context.resources->UploadConstants(&params, sizeof(params));
    if (!constants.IsValid())
    {
        throw std::runtime_error("Light tile constants upload failed.");
    }

    const auto depth = m_depth;
    const auto countBuffer = m_tileCountBuffer;
    const auto listBuffer = m_tileListBuffer;
    const uint32_t tilesX = m_tileCountX, tilesY = m_tileCountY;
    auto* device = context.resources;
    graph.AddPass(
        "METEOR.Scene.LightTiles",
        {{depth, RHIResourceState::ShaderResource, readAccess},
         {counts, RHIResourceState::UnorderedAccess, writeAccess},
         {indices, RHIResourceState::UnorderedAccess, writeAccess}},
        [=, cull = m_cullPSO.GetGeneration()](const EnhancedRenderGraph::ExecuteContext& execution) {
            const uint32_t tileTotal = tilesX * tilesY;
            const RHIBindingDesc srvs[] = {RHIBindingDesc::SrvDepth(execution.ResolveHandle(depth))};
            const RHIBindingDesc uavs[] = {
                RHIBindingDesc::UavBuffer(countBuffer, tileTotal * 2, sizeof(uint32_t)),
                RHIBindingDesc::UavBuffer(listBuffer, tileTotal * kMaxLightsPerTile, sizeof(uint32_t)),
            };
            const RHIBindingTable srvTable = device->CreateBindings(srvs);
            const RHIBindingTable uavTable = device->CreateBindings(uavs);
            if (!srvTable.IsValid() || !uavTable.IsValid())
            {
                throw std::runtime_error("Light tile bindings unavailable.");
            }
            RHIEncoder& encoder = *execution.encoder;
            encoder.SetPipeline(RHIBindPoint::Compute, cull->GetHandle());
            encoder.SetConstantBuffer(RHIBindPoint::Compute, 0, constants);
            encoder.SetBindings(RHIBindPoint::Compute, 1, srvTable);
            encoder.SetRootBuffer(RHIBindPoint::Compute, 2, lights);
            encoder.SetBindings(RHIBindPoint::Compute, 3, uavTable);
            encoder.Dispatch(tilesX, tilesY, 1);
        });

    m_lighting.lights = lights;
    m_lighting.counts = m_tileCountBuffer;
    m_lighting.indices = m_tileListBuffer;
    m_lighting.graphCounts = counts;
    m_lighting.graphIndices = indices;
    m_lighting.reference = m_reference;
}

void EnhancedLightTilePass::Shutdown()
{
    if (auto* device = m_resourceRetirement.Device())
    {
        if (m_tileCountBuffer.IsValid()) device->ReleaseBuffer(m_tileCountBuffer);
        if (m_tileListBuffer.IsValid()) device->ReleaseBuffer(m_tileListBuffer);
    }
    m_resourceRetirement.ClearAfterIdle();
    m_cullPSO = {};
    m_depth = {};
    m_lighting = {};
    m_tileCountBuffer = {};
    m_tileListBuffer = {};
    m_allocatedTiles = 0;
    m_tileCountState = RHIResourceState::Common;
    m_tileListState = RHIResourceState::Common;
    m_tileCountX = 0;
    m_tileCountY = 0;
}
