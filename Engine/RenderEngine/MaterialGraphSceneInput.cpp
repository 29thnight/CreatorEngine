#include "MaterialGraphSceneInput.h"
#include "Render/Graph/ShadowCasterBounds.h"
#include "Render/Graph/EnhancedDrawIdentity.h"
#include "Material.h"
#include "Assets/ModelGeometryPayload.h"
#include "../EngineDiagnostics/ProfileScope.h"

#include <algorithm>
#include <atomic>
#include <bit>
#include <cmath>
#include <limits>

namespace material_graph
{
    namespace
    {
        bool Fail(std::string& error, std::string message)
        {
            error = std::move(message);
            return false;
        }

        bool Finite(std::span<const float> values)
        {
            return std::ranges::all_of(values, [](float value) { return std::isfinite(value) && std::abs(value) <= 1e6f; });
        }
    } // namespace

    std::shared_ptr<const SceneMaterialSource> SceneMaterialSource::Capture(const Material& material)
    {
        SceneCoverage queue;
        switch (material.m_renderingMode)
        {
        case MaterialRenderingMode::Opaque:
            queue = SceneCoverage::Opaque;
            break;
        case MaterialRenderingMode::Masked:
            queue = SceneCoverage::Masked;
            break;
        case MaterialRenderingMode::Transparent:
            queue = SceneCoverage::Blended;
            break;
        default:
            queue = static_cast<SceneCoverage>(255);
            break;
        }
        return Capture(material.GetMaterialGraphInstance(), queue, material.m_doubleSided,
                       static_cast<std::uint64_t>(material.m_materialGuid));
    }

    std::shared_ptr<const SceneMaterialSource> SceneMaterialSource::Capture(own::shared_owner<const Instance> instance,
                                                                            SceneCoverage queue, bool doubleSided,
                                                                            std::uint64_t materialSlot)
    {
        if (!instance)
        {
            return {};
        }
        auto source = std::make_shared<SceneMaterialSource>();
        source->instance = std::move(instance);
        source->materialSlot = materialSlot;
        source->coverage.flags = EnhancedMaterialCoverage::Enabled;
        if (doubleSided)
        {
            source->coverage.flags |= EnhancedMaterialCoverage::DoubleSided;
        }
        if (queue == SceneCoverage::Masked)
        {
            source->coverage.flags |= EnhancedMaterialCoverage::Masked;
        }
        else if (queue == SceneCoverage::Blended)
        {
            source->coverage.flags |= EnhancedMaterialCoverage::Blended;
        }
        else if (queue != SceneCoverage::Opaque)
        {
            // Keep the graph owner so an invalid policy cannot fall back to legacy.
            source->coverage.flags = 0;
        }
        return source;
    }

    bool SceneViewInput::Seal(const SceneInputView& view, std::span<const EnhancedDrawItem> draws,
                              const SceneInputBudget& budget, std::shared_ptr<const SceneViewInput>& result,
                              std::string& error,
                              own::shared_owner<const assets::ModelAssetGenerationPins> modelPins,
                              own::shared_owner<InstanceFramePins> materialPins,
                              const assets::ModelGeometryPreparationPins* geometryPins)
    {
        ce::profile_scope profile{ce::marker<"MaterialSceneInputSeal">()};
        const auto& camera = view.camera;
        const std::array eye{camera.eyePosition.x, camera.eyePosition.y, camera.eyePosition.z, 0.f};
        if (!view.frameId || !view.sceneEpoch || !view.viewId || !view.width || !view.height ||
            std::uint64_t(view.width) * view.height > UINT32_MAX || !budget.draws || draws.size() > budget.draws ||
            !budget.chunks || !Finite(eye) || !Finite(std::bit_cast<std::array<float, 16>>(camera.view)) ||
            !Finite(std::bit_cast<std::array<float, 16>>(camera.projection)) || !math::try_inverse(camera.view) ||
            !math::try_inverse(camera.projection))
        {
            return Fail(error,
                        "Scene graph input needs an identified frame/view, invertible finite camera and draw budget.");
        }
        if (!enhanced_draw::ValidateGeometryIdentities(draws, error))
        {
            return false;
        }
        auto candidate = std::shared_ptr<SceneViewInput>(new SceneViewInput);
        candidate->modelPins_ = std::move(modelPins);
        auto collectedPins = materialPins ? own::shared_owner<InstanceFramePins>{}
            : own::make_shared<InstanceFramePins>();
        candidate->materialPins_ = materialPins ? std::move(materialPins) : collectedPins;
        candidate->view_ = view;
        candidate->viewProjection_ = camera.view * camera.projection;
        if (!Finite(std::bit_cast<std::array<float, 16>>(candidate->viewProjection_)))
        {
            return Fail(error, "Scene graph view/projection product exceeds the spatial input range.");
        }
        static std::atomic<std::uint64_t> nextRevision{1};
        auto revision = nextRevision.load(std::memory_order_relaxed);
        while (revision != UINT64_MAX &&
               !nextRevision.compare_exchange_weak(revision, revision + 1, std::memory_order_relaxed))
        {
        }
        if (revision == UINT64_MAX)
        {
            return Fail(error, "Scene graph input revision is exhausted.");
        }
        candidate->surface_ = {eye, view.sceneEpoch, revision, revision};
        candidate->draws_.reserve(draws.size());
        for (std::size_t sourceIndex = 0; sourceIndex < draws.size(); ++sourceIndex)
        {
            const auto& draw = draws[sourceIndex];
            const auto instance = draw.GraphInstance();
            if (!draw.geometryKey || !instance || !instance->generation || !instance->generation->generation ||
                instance->generation->assetId != instance->description.graphId || draw.materialSnapshot ||
                draw.forwardMaterialSnapshot)
            {
                return Fail(error, "Scene graph draw needs owning geometry/instance and cannot mix ShaderMeta snapshots.");
            }
            const auto& geometry = draw.modelMeshView;
            if (!geometry.IsComplete() || geometry.vertexBytes / geometry.vertexStride > budget.mesh.maxSourceVertices)
            {
                return Fail(error, "Scene graph draw exceeds the source geometry budget.");
            }
            SceneDrawInput input;
            input.sourceIndex = sourceIndex;
            input.geometryKey = draw.geometryKey;
            input.viewDepth = math::dot(draw.worldMatrix.translation() - view.camera.eyePosition, view.camera.forward);
            if (!std::isfinite(input.viewDepth)) return Fail(error, "Scene draw has a non-finite sorting depth.");
            input.mesh = geometry.handle;
            if (geometry.handle.domain == assets::ModelMeshDomain::Granular)
            {
                if (!candidate->modelPins_ || !geometryPins)
                {
                    return Fail(error, "Scene graph granular geometry needs descriptor and CPU-use pin tables.");
                }
                const auto& pins = candidate->modelPins_->meshes;
                const auto pin = std::ranges::find_if(pins, [&](const auto& owner)
                {
                    return owner && assets::MakeModelMeshHandle(*owner) == input.mesh;
                });
                const auto payload = std::ranges::find_if(geometryPins->entries, [&](const auto& entry)
                {
                    return entry.handle == input.mesh;
                });
                if (pin == pins.end() || payload == geometryPins->entries.end() || !payload->payload
                    || &**pin != geometry.sourceDescriptor || &*payload->payload != geometry.sourcePayload
                    || !payload->payload->Matches(**pin) || geometry.SourceMesh() != &payload->payload->mesh)
                {
                    return Fail(error, "Scene graph geometry has no exact descriptor/payload provenance in its pin tables.");
                }
                input.modelPinIndex = static_cast<std::size_t>(pin - pins.begin());
            }
            else
            {
                input.model = {geometry.handle.modelId, geometry.handle.generation};
                if (candidate->modelPins_)
                {
                    const auto& pins = candidate->modelPins_->generations;
                    const auto pin = std::ranges::find_if(pins, [&](const auto& owner)
                    {
                        return owner && owner->Handle() == input.model;
                    });
                    if (pin == pins.end())
                    {
                        return Fail(error, "Scene graph geometry has no exact model generation in its frame pin table.");
                    }
                    input.modelPinIndex = static_cast<std::size_t>(pin - pins.begin());
                }
            }
            input.materialSlot = draw.materialGraphSlot;
            input.materialPinIndex = collectedPins
                ? collectedPins->Retain(draw.materialGraphInstance)
                : candidate->materialPins_->Find(*instance);
            input.material = candidate->materialPins_->Borrow(input.materialPinIndex);
            if (!input.material || &*input.material != &*instance
                || InstanceFramePins::Identity(*input.material) != InstanceFramePins::Identity(*instance))
            {
                return Fail(error, "Scene graph draw has no exact instance in its frame pin table.");
            }
            input.coverage = draw.coverage;
            if (!ClassifySceneCoverage(input.coverage, input.queue, error))
            {
                return false;
            }
            // Raster computes the texture footprint from perspective UV derivatives.
            // Zero vertex LOD is transport initialization, not a material footprint.
            if (!MeshSurfacePlan::BuildForScene(draw, candidate->surface_, budget.mesh, input.geometry, error))
            {
                error = "Scene graph geometry " + std::to_string(draw.geometryKey) + ": " + error;
                return false;
            }
            // Bounds consume source bytes and palette only after their full validation.
            const auto shadowBounds = shadow_math::WorldBounds(draw);
            input.shadowCenter = shadowBounds.center;
            input.shadowRadius = shadowBounds.radius;
            const auto& cost = input.geometry->Cost();
            auto& total = candidate->cost_;
            const auto chunks = input.geometry->Chunks().size();
            if (chunks > budget.chunks - total.chunks ||
                cost.cpuPayloadBytes > budget.cpuPayloadBytes - total.cpuPayloadBytes ||
                cost.gpuPayloadBytes > budget.gpuPayloadBytes - total.gpuPayloadBytes)
            {
                return Fail(error, "Scene graph view exceeds its aggregate chunk/CPU/GPU payload budget.");
            }
            total.sourceVertices += cost.sourceVertices;
            total.triangles += cost.triangles;
            total.chunks += chunks;
            total.cpuPayloadBytes += cost.cpuPayloadBytes;
            total.gpuPayloadBytes += cost.gpuPayloadBytes;
            candidate->draws_.push_back(std::move(input));
        }
        result = std::move(candidate);
        error.clear();
        return true;
    }
} // namespace material_graph
