#include "GraphSceneFixture.h"

#include "Assets/AssetIdentityProfile.h"
#include "Assets/ModelMaterialGraph.h"
#include "Assets/ModelVertexLayout.h"
#include "../../../../../../Tools/regression/support/CookedMeshFixture.h"
#include "DataSystem.h"
#include "MaterialGraphSceneInput.h"
#include "Mesh.h"
#include "PathFinder.h"
#include "RHI/DX12/DX12DeviceResources.h"
#include "RHI/RHIShaderCompiler.h"
#include "RHI/RHISubmissionThread.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <thread>

namespace
{
    // Fixed test identity, never a product asset.
    constexpr const char* kFixtureGraphGuid = "6a1f3c52-4d1e-4b8a-9c07-5e2d8b1f0a63";
    constexpr auto kProgramTimeout = std::chrono::seconds(90);

    void AssignFixtureIdentity(RHIModelMeshView& view, std::uint32_t index, std::uint32_t attributes)
    {
        char meshId[40]{};
        std::snprintf(meshId, sizeof(meshId), "22222222-2222-8222-8222-%012x", index + 1u);
        assets::TryParseCanonicalUuidV8("11111111-1111-8111-8111-111111111111", view.handle.modelId);
        assets::TryParseCanonicalUuidV8(meshId, view.handle.meshId);
        view.handle.generation = 1;
        view.vertexAttributeMask = attributes;
        view.vertexStride = assets::StrideOf(attributes);
        view.vertexLayoutHash = assets::VertexLayoutHash(attributes);
    }
}

bool GraphSceneFixture::Initialize(const EnhancedFrameContext& context, std::string& error, float alpha)
{
    if (m_initialized)
    {
        return true;
    }
    auto source = assets::BuildDefaultMaterialGraph(error);
    if (!source)
    {
        error = "Default material graph: " + error;
        return false;
    }
    if (alpha != 1.f)
    {
        // The default graph has one surface BSDF; author its actual alpha socket.
        bool assigned = false;
        for (const auto& node : source->graph.Nodes())
            for (const auto& pin : node.pins)
                if (pin.Identifier() == "Alpha")
                {
                    source->graph.SetSocketValue(pin.id, double(alpha));
                    assigned = true;
                }
        if (!assigned) { error = "Fixture graph has no Alpha socket."; return false; }
    }
    m_generation = DataSystem::CompileMaterialGraphAuthoring(*source, FileGuid(std::string(kFixtureGraphGuid)),
        PathFinder::RelativeToShader("DefaultPassShader"), PathFinder::CachePath("Lattice"), error);
    if (!m_generation)
    {
        error = "Default material graph compile: " + error;
        return false;
    }
    material_graph::InstanceDescription description;
    description.graphId = m_generation->assetId;
    const auto noTextures = [](const experiment::AssetId&, LX::LXColorSpace, std::string& why)
    {
        why = "The shadow caster fixture graph has no texture inputs.";
        return own::shared_owner<const Texture>{};
    };
    if (!material_graph::BuildInstance(m_generation, description, noTextures, m_instance, error))
    {
        error = "Default material graph instance: " + error;
        return false;
    }
    if (!m_host.RequestProgram(context, m_generation, error))
    {
        error = "Scene program request: " + error;
        return false;
    }
    const auto deadline = std::chrono::steady_clock::now() + kProgramTimeout;
    while (!m_host.IsProgramReady(m_generation, RHIShaderCompiler::GetOutput()))
    {
        m_host.PollPrograms(context);
        const auto stats = m_host.ProgramStats();
        if (stats.failedPreparations || std::chrono::steady_clock::now() >= deadline)
        {
            error = "Scene program preparation: " + stats.lastError;
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    m_initialized = true;
    return true;
}

std::uint32_t GraphSceneFixture::AddMesh(const std::vector<Vertex>& vertices,
    const std::vector<std::uint32_t>& indices)
{
    const auto index = static_cast<std::uint32_t>(m_meshes.size());
    // Legacy test vertices carry skin as float4 indices. Any active weight
    // selects the product skin layout (RGBA8Uint indices, float4 weights).
    const bool skinned = std::any_of(vertices.begin(), vertices.end(), [](const Vertex& vertex)
    {
        return vertex.boneWeights.x > 0.f || vertex.boneWeights.y > 0.f
            || vertex.boneWeights.z > 0.f || vertex.boneWeights.w > 0.f;
    });
    auto& mesh = m_meshes.emplace_back();
    auto& view = mesh.view;
    AssignFixtureIdentity(view, index,
        skinned ? assets::kCoreVertexAttributes | assets::kSkinVertexAttributes : assets::kCoreVertexAttributes);
    mesh.vertices.resize(vertices.size() * view.vertexStride);
    for (std::size_t i = 0; i < vertices.size(); ++i)
    {
        const auto& source = vertices[i];
        auto* target = mesh.vertices.data() + i * view.vertexStride;
        const float tangent[4]{source.tangent.x, source.tangent.y, source.tangent.z, 1.f};
        const auto write = [&](assets::VertexAttribute attribute, const void* value, std::size_t bytes)
        {
            std::memcpy(target + assets::OffsetOf(view.vertexAttributeMask, attribute), value, bytes);
        };
        write(assets::VertexAttribute::Position, &source.position, 12);
        write(assets::VertexAttribute::Normal, &source.normal, 12);
        write(assets::VertexAttribute::Tangent, tangent, 16);
        write(assets::VertexAttribute::Uv0, &source.uv0, 8);
        if (skinned)
        {
            const std::uint8_t bones[4]{static_cast<std::uint8_t>(source.boneIndices.x),
                static_cast<std::uint8_t>(source.boneIndices.y), static_cast<std::uint8_t>(source.boneIndices.z),
                static_cast<std::uint8_t>(source.boneIndices.w)};
            write(assets::VertexAttribute::BoneIndices, bones, 4);
            write(assets::VertexAttribute::BoneWeights, &source.boneWeights, 16);
        }
    }
    mesh.indices = indices;
    view.vertexData = mesh.vertices.data();
    view.vertexBytes = mesh.vertices.size();
    view.indexData = mesh.indices.data();
    view.indexCount = static_cast<std::uint32_t>(mesh.indices.size());
    CookMesh(index);
    return index;
}

void GraphSceneFixture::CookMesh(uint32_t index)
{
    auto& stored = m_meshes[index];
    CookedMeshFixture cooked;
    std::string error;
    if (!CookMeshFixture(stored.view, cooked, error))
        throw std::runtime_error("METEOR fixture: " + error);
    stored.descriptor = std::move(cooked.descriptor);
    stored.payload = std::move(cooked.payload);
    stored.view = cooked.view;
}

std::uint32_t GraphSceneFixture::AddView(const RHIModelMeshView& source)
{
    const auto index = static_cast<std::uint32_t>(m_meshes.size());
    auto& mesh = m_meshes.emplace_back();
    auto& view = mesh.view;
    AssignFixtureIdentity(view, index, source.vertexAttributeMask);
    const auto* bytes = static_cast<const std::uint8_t*>(source.vertexData);
    mesh.vertices.assign(bytes, bytes + source.vertexBytes);
    mesh.indices.assign(source.indexData, source.indexData + source.indexCount);
    view.vertexData = mesh.vertices.data();
    view.vertexBytes = mesh.vertices.size();
    view.indexData = mesh.indices.data();
    view.indexCount = static_cast<std::uint32_t>(mesh.indices.size());
    CookMesh(index);
    return index;
}

EnhancedDrawItem GraphSceneFixture::Caster(const EnhancedDrawItem& source)
{
    // Isolated fixtures may leave geometryKey zero on legacy meshes; the mesh
    // object then names the geometry within this fixture.
    const auto key = source.geometryKey ? source.geometryKey : reinterpret_cast<std::size_t>(source.mesh);
    std::uint32_t index{};
    if (const auto found = m_sourceMeshes.find(key); key && found != m_sourceMeshes.end())
    {
        index = found->second;
    }
    else if (source.modelMeshView.vertexData && source.modelMeshView.indexData && source.modelMeshView.vertexStride)
    {
        index = AddView(source.modelMeshView);
        m_sourceMeshes.emplace(key, index);
    }
    else if (source.mesh)
    {
        index = AddMesh(source.mesh->GetVertices(), source.mesh->GetIndices());
        m_sourceMeshes.emplace(key, index);
    }
    else
    {
        // No geometry: the seal rejects geometryKey zero with a precise error.
        return {};
    }
    auto draw = Draw(index, source.worldMatrix);
    // Keep an authored face/mask policy. Blended casters are excluded by the
    // Scene host exactly as in the product.
    const auto& coverage = source.materialSnapshot ? source.materialSnapshot->coverage : source.coverage;
    if (coverage.flags != 0)
    {
        draw.coverage = coverage;
    }
    draw.bonePalette = source.bonePalette;
    draw.boneCount = source.boneCount;
    draw.animatorKey = source.animatorKey;
    draw.temporalObjectId = source.temporalObjectId;
    draw.temporalIncarnation = source.temporalIncarnation;
    draw.temporalInstanceId = source.temporalInstanceId;
    return draw;
}

EnhancedDrawItem GraphSceneFixture::Draw(std::uint32_t mesh, const math::matrix4x4& world) const
{
    EnhancedDrawItem draw{};
    draw.modelMeshView = m_meshes.at(mesh).view;
    draw.geometryKey = mesh + 1u;
    draw.worldMatrix = world;
    // Synthetic test meshes have no authored face policy. Legacy coverage flags
    // 0 skipped face rejection entirely; the graph equivalent is double-sided.
    draw.coverage.flags = EnhancedMaterialCoverage::Enabled | EnhancedMaterialCoverage::DoubleSided;
    draw.materialGraphInstance = m_instance;
    draw.materialGraphSlot = 1;
    return draw;
}

void GraphSceneFixture::BuildPins(assets::ModelAssetGenerationPins& descriptors,
    assets::ModelGeometryPreparationPins& geometry) const
{
    for (const auto& mesh : m_meshes)
    {
        descriptors.meshes.push_back(mesh.descriptor);
        geometry.entries.push_back({mesh.view.handle, mesh.payload});
    }
}

bool GraphSceneFixture::Prepare(EnhancedFrameContext& context, EnhancedShadowPass& shadow, std::string& error)
{
    if (!m_initialized || !context.shadowDraws || !context.camera)
    {
        error = "Graph shadow fixture needs initialization, casters and a camera.";
        return false;
    }
    if (!shadow.PrepareGraphFrame(context, error))
    {
        return false;
    }
    // The Scene host requires the sealed view and the frame context to name the
    // same scene epoch. Isolated tests leave it zero; one fixed epoch keeps the
    // host's cached geometry valid across their frames.
    context.sceneEpoch = m_sceneEpoch;
    const material_graph::SceneInputView view{context.frameId, m_sceneEpoch, 1, 1,
        context.width, context.height, *context.camera};
    own::shared_owner<const material_graph::SceneViewInput> input;
    auto pins = own::make_shared<assets::ModelAssetGenerationPins>();
    assets::ModelGeometryPreparationPins geometryPins;
    BuildPins(*pins, geometryPins);
    if (!material_graph::SceneViewInput::Seal(view, *context.shadowDraws, {}, input, error, pins, {}, &geometryPins))
    {
        error = "Graph shadow seal: " + error;
        return false;
    }
    // The live renderer's lookup budget: runtime evaluation, no full-view
    // capture targets for the private camera route.
    const material_graph::SceneHostBudget budget{.lookupApproximate = true, .lookupRuntimeEvaluation = true};
    if (!m_host.PrepareResidency(context, input, error) ||
        !m_host.Prepare(context, input, {}, {}, {}, shadow.GetShadowData(), budget, error, 1))
    {
        error = "Graph shadow prepare: " + error;
        return false;
    }
    m_cameraContext = context;
    return true;
}

namespace
{
    // A cleared render-target input the host's camera route consumes.
    RGHandle DeclareClearedInput(EnhancedRenderGraph& graph, IRenderDeviceServices& resources,
        const EnhancedFrameContext& context, RHIFormat format, float value, const char* name)
    {
        RGTextureDesc desc{};
        desc.width = context.width;
        desc.height = context.height;
        desc.format = format;
        desc.allowRenderTarget = true;
        desc.name = name;
        desc.clearColor[0] = value;
        RGHandle texture = graph.CreateTexture(desc);
        const bool versioned = graph.GetSchedulingMode() == RGSchedulingMode::ExplicitVersioned;
        if (versioned)
        {
            texture = graph.Write(texture);
        }
        const auto access = graph.GetSchedulingMode() == RGSchedulingMode::DeclarationOrder
            ? RGAccessMode::LegacyState : RGAccessMode::Write;
        graph.AddPass(name, {{texture, RHIResourceState::RenderTarget, access}},
            [&resources, texture, value](const EnhancedRenderGraph::ExecuteContext& execution)
            {
                const RHITextureHandle handle = execution.ResolveHandle(texture);
                const auto targets = resources.CreateRenderTargets({&handle, 1}, nullptr);
                if (!targets.IsValid())
                {
                    throw std::runtime_error("Graph shadow fixture input target is unavailable.");
                }
                // Must equal desc.clearColor: a mismatched clear is a debug-layer warning.
                const float clear[]{value, 0.f, 0.f, 0.f};
                execution.encoder->BindRenderTargets(targets);
                execution.encoder->ClearRenderTargets(targets, clear);
            });
        return texture;
    }
}

RGHandle GraphSceneFixture::Declare(EnhancedRenderGraph& graph, EnhancedShadowPass& shadow,
    const EnhancedFrameContext& context)
{
    // Product node order: geometry, culling, then shadow and surface raster.
    m_host.DeclareGeometry(graph);
    m_host.DeclareCulling(graph);
    shadow.DeclareGraphTargets(graph, context);
    const RGHandle shadowMap = m_host.DeclareShadow(graph, shadow.GetShadowMap());

    // The host's camera route records the lighting lookup that publication and
    // GPU memory retirement require. It lands in private targets only.
    m_cameraTargets.Declare(graph, m_cameraContext);
    const auto outputs = m_host.DeclareGBuffer(graph, m_cameraTargets.GetOutputs());
    auto& resources = *m_cameraContext.resources;
    const RGHandle lighting = material_graph::SceneHost::DeclareHdrTarget(graph, m_cameraContext);
    const RGHandle ambientOcclusion = DeclareClearedInput(graph, resources, m_cameraContext,
        RHIFormat::RG16Float, 1.f, "GraphShadowFixture.AO");
    const RGHandle color = m_host.DeclareColor(graph, outputs, lighting, ambientOcclusion, shadowMap);
    // Sink: without a consumer the graph would cull the camera route.
    const auto read = graph.GetSchedulingMode() == RGSchedulingMode::DeclarationOrder
        ? RGAccessMode::LegacyState : RGAccessMode::Read;
    graph.AddPass("GraphShadowFixture.Sink", {{color, RHIResourceState::ShaderResource, read}}, nullptr, true);
    return shadowMap;
}

bool GraphSceneFixture::Publish(DX12DeviceResources& resources, std::uint64_t frameId, std::string& error,
    const RHISubmissionTicket& ticket)
{
    // Callers return early on failure and destroy their graph. Retire the
    // submitted frame first so no transient is released while GPU work runs.
    const auto failAfterIdle = [&](const char* stage)
    {
        resources.WaitForGpu();
        error = std::string(stage) + ": " + error;
        return false;
    };
    // Same order as the product and material probes: CPU submission, material
    // cache publication, GPU completion, then completion retirement.
    if (!GetRHISubmissionThread().DrainSubmissions(&resources, error))
    {
        return failAfterIdle("Graph shadow submission drain");
    }
    const auto* batch = ticket.IsValid() ? ticket.GetRecordedBatch() : nullptr;
    const RHICompletionPoint completion = batch
        ? batch->GetCompletionPoint() : RHICompletionPoint{resources.GetLastSignaledFenceValue()};
    if (!m_host.PublishSubmittedCache(frameId, completion, error, ticket))
    {
        return failAfterIdle("Graph shadow Scene publication");
    }
    return true;
}

bool GraphSceneFixture::CompleteSubmission(DX12DeviceResources& resources, std::string& error)
{
    resources.WaitForGpu();
    return GetRHISubmissionThread().Drain(&resources, error);
}

void GraphSceneFixture::Shutdown()
{
    if (m_initialized)
    {
        m_host.ShutdownAfterIdle();
        m_cameraTargets.Shutdown();
        m_initialized = false;
    }
}

bool GraphSceneFixture::PrepareFrame(EnhancedFrameContext& context, std::string& error,
    const EnhancedShadowData& shadow)
{
    if (!m_initialized || !context.draws || !context.camera) {
        error = "METEOR surface fixture needs initialized graph, geometry and camera."; return false;
    }
    context.sceneEpoch = m_sceneEpoch;
    m_surfaceDraws.clear();
    for (const auto& draw : *context.draws) m_surfaceDraws.push_back(Caster(draw));
    const material_graph::SceneInputView view{context.frameId, m_sceneEpoch, 1, 1,
        context.width, context.height, *context.camera};
    own::shared_owner<const material_graph::SceneViewInput> input;
    auto pins = own::make_shared<assets::ModelAssetGenerationPins>();
    assets::ModelGeometryPreparationPins geometryPins;
    BuildPins(*pins, geometryPins);
    if (!material_graph::SceneViewInput::Seal(view, m_surfaceDraws, {}, input, error, pins, {}, &geometryPins)) return false;
    const material_graph::SceneHostBudget budget{.lookupApproximate = true, .lookupRuntimeEvaluation = true};
    m_cameraContext = context;
    return m_host.PrepareResidency(context, input, error) &&
        m_host.Prepare(context, input, {}, {}, {}, shadow, budget, error, 1);
}

void GraphSceneFixture::DeclareSurface(EnhancedRenderGraph& graph, const EnhancedFrameContext& context)
{
    m_host.DeclareGeometry(graph);
    m_host.DeclareCulling(graph);
    m_cameraTargets.Declare(graph, context);
    m_surfaceOutputs = m_host.DeclareGBuffer(graph, m_cameraTargets.GetOutputs());
}

RGHandle GraphSceneFixture::DeclareColor(EnhancedRenderGraph& graph, const EnhancedFrameContext& context,
    RGHandle lighting, RGHandle shadowMap)
{
    const auto ao = DeclareClearedInput(graph, *context.resources, context,
        RHIFormat::RG16Float, 1.f, "METEOR.SurfaceFixture.AO");
    return m_host.DeclareColor(graph, m_surfaceOutputs, lighting, ao, shadowMap);
}

void GraphSceneFixture::Declare(EnhancedRenderGraph& graph, const EnhancedFrameContext& context)
{
    DeclareSurface(graph, context);
    const auto lighting = material_graph::SceneHost::DeclareHdrTarget(graph, context);
    const auto color = DeclareColor(graph, context, lighting, {});
    const auto read = graph.GetSchedulingMode() == RGSchedulingMode::DeclarationOrder ? RGAccessMode::LegacyState : RGAccessMode::Read;
    graph.AddPass("METEOR.SurfaceFixture.Sink", {{color,RHIResourceState::ShaderResource,read}}, nullptr, true);
}

uint32_t GraphSceneFixture::GetLastSkinnedCount() const
{
    return static_cast<uint32_t>(std::count_if(m_surfaceDraws.begin(),m_surfaceDraws.end(),
        [](const auto& draw){ return draw.bonePalette && draw.boneCount; }));
}
uint32_t GraphSceneFixture::GetLastBonePaletteCount() const
{
    std::vector<const math::matrix4x4*> palettes;
    for (const auto& draw : m_surfaceDraws)
        if (draw.boneCount && draw.bonePalette && std::find(palettes.begin(),palettes.end(),draw.bonePalette)==palettes.end())
            palettes.push_back(draw.bonePalette);
    return static_cast<uint32_t>(palettes.size());
}
