#pragma once

#include "EnhancedSceneRenderer.h"
#include "EnhancedCameraReplayInput.h"
#include "EnhancedDrawReplayInput.h"
#include "EnhancedLatticeReplayInput.h"
#include "../Temporal/TemporalRuntimeControl.h"
#include "../Temporal/TemporalMeasurementProvenance.h"
#include <optional>
#include <stdexcept>
#include "../Core/EnhancedLivePipelineDesc.h"
#include "../Passes/Geometry/EnhancedGBufferPass.h"
#include "../Graph/EnhancedDrawSealLedger.h"
#include "../../Texture.h"
#include "../../RHI/IRHIDeviceResources.h"
#include "../../MaterialGraphSceneInput.h"
#include <AuthoringRymlErrorPolicy.h>
#include <ryml/ryml.hpp>
#include <ryml/ryml_std.hpp>
#include <c4/yml/emit.hpp>
#include <filesystem>
#include <fstream>
#include <cmath>
#include <limits>
#include <cstring>

// Explicit diagnostic capture only. The render thread owns this object and its
// readbacks. Release must run after submission completion (or frame abort).
struct EnhancedPbrCapture
{
    // Acquired when the request is created, before the next frame is configured.
    std::optional<TemporalNativeCaptureExclusion> nativeExclusion{ std::in_place };
    TemporalMeasurementProvenance temporalProvenance;
    uint64_t capturedViewId{}, capturedSceneEpoch{}, capturedRealFrameId{};
    EnhancedLivePbrCaptureStatus result;
    EnhancedLiveBackend resourceBackend{ EnhancedLiveBackend::DX12 };
    EnhancedLiveDisplayTarget target{ EnhancedLiveDisplayTarget::Game };
    uint64_t afterFrameId{};
    bool controlled{ false }; // Static scene repeatability; not a simulation clock.
    bool replayExtensions{ false }; // Optional archive/replay diagnostics, not BASE-0 acceptance.
    bool latticeReplayExtension{ false }; // Independent material archive opt-in.
    std::optional<EnhancedCameraReplayInput> cameraReplay;
    std::vector<uint8_t> cameraInputBytes;
    std::optional<EnhancedDrawReplayInput> drawReplay;
    std::vector<uint8_t> drawInputBytes;
    std::optional<EnhancedLatticeReplayInput> latticeReplay;
    std::vector<uint8_t> latticeInputBytes;
    ryml::Tree manifest;
    std::array<RHIReadback, 7> readbacks{};
    struct StageReadback
    {
        std::string name;
        RGHandle source;
        RHIReadback readback;
    };
    struct StageDiagnostic
    {
        std::string name;
        size_t readbackIndex{};
    };
    std::vector<StageReadback> stageReadbacks;
    std::vector<StageDiagnostic> stages;

    bool DeclareStage(IRenderDeviceServices& resources, EnhancedRenderGraph& graph,
        const LiveBlackboard& blackboard, const LivePassNode& node,
        uint32_t width, uint32_t height, std::string& error)
    {
        graph.SetMeasurementDomain(RGMeasurementDomain::Capture);
        const auto affectsHdr = [](const auto& slots) {
            return std::find(slots.begin(), slots.end(), LiveSlots::kLitColor) != slots.end();
        };
        if (!affectsHdr(node.writes) && !affectsHdr(node.modifies))
        {
            return true;
        }
        const auto handle = blackboard.Get(LiveSlots::kLitColor);
        if (!handle.IsValid())
        {
            error = "missing stage HDR: " + node.name;
            return false;
        }
        const auto name = "hdr-" + std::to_string(stages.size()) + "-" + node.name;
        // Disabled stages may forward the same image without modifying it. A
        // versioned handle proves image identity; legacy handles do not, because
        // later writes may retain version zero. Keep every named association.
        if (graph.GetSchedulingMode() == RGSchedulingMode::ExplicitVersioned)
        {
            for (size_t index = 0; index < stageReadbacks.size(); ++index)
            {
                const auto& source = stageReadbacks[index].source;
                if (source.index == handle.index && source.version == handle.version &&
                    source.kind == handle.kind && source.epoch == handle.epoch)
                {
                    stages.push_back({name, index});
                    return true;
                }
            }
        }
        RHIReadback readback{};
        if (!resources.CreateReadback(width, height, RHIFormat::RGBA16Float, 1, readback, error))
        {
            return false;
        }
        stages.push_back({name, stageReadbacks.size()});
        stageReadbacks.push_back({name, handle, readback});
        graph.AddPass("PBR.Stage." + node.name, { { handle, RHIResourceState::CopySource,
                graph.GetSchedulingMode() != RGSchedulingMode::DeclarationOrder
                    ? RGAccessMode::Read : RGAccessMode::LegacyState } },
            [handle, readback](const EnhancedRenderGraph::ExecuteContext& context)
            { context.encoder->CopyToReadback(readback, context.ResolveHandle(handle)); }, true);
        return true;
    }

    static void WriteMeshIdentity(ryml::NodeRef item, const assets::ModelMeshHandle& handle)
    {
        item["meshId"] << FileGuid(handle.meshId).ToString();
        item["meshDomain"] << (handle.domain == assets::ModelMeshDomain::Granular ? "granular" : "aggregate");
        if (handle.domain == assets::ModelMeshDomain::Granular)
        {
            item["assetId"] << FileGuid(handle.asset.key.assetId.value).ToString();
            item["subassetId"] << FileGuid(handle.asset.key.subassetId.value).ToString();
            item["resolverRevision"] << handle.resolverRevision;
            item["representation"] << handle.blob.representation;
            item["artifactSchema"] << handle.blob.schemaVersion;
            item["artifactBytes"] << handle.blob.byteSize;
            item["targetPlatform"] << handle.blob.targetPlatform;
            item["targetAbi"] << handle.blob.targetAbi;
            item["contentSha256"] |= ryml::SEQ;
            for (const auto byte : handle.blob.contentSha256)
            {
                item["contentSha256"].append_child() << static_cast<uint32_t>(byte);
            }
        }
        else
        {
            item["modelId"] << FileGuid(handle.modelId).ToString();
            item["modelGeneration"] << handle.generation;
        }
    }

    void Begin(const EnhancedLiveFramePacket& frame, const EnhancedLiveViewPacket& view,
        EnhancedLiveBackend backend, std::span<const EnhancedDrawItem> opaque,
        std::span<const EnhancedDrawItem> transparent,
        std::span<const EnhancedLight> lights, const std::string& skyBoxPath)
    {
        result.state = EnhancedPbrCaptureState::Recording;
        resourceBackend = backend;
        result.frameId = frame.frameId;
        capturedViewId = view.key.viewId;
        capturedSceneEpoch = frame.sceneEpoch;
        capturedRealFrameId = view.displayTarget == EnhancedLiveDisplayTarget::Game && frame.temporalRealFrameId
            ? frame.temporalRealFrameId : frame.frameId;
        Authoring::EnsureRymlErrorPolicy();
        auto root = manifest.rootref();
        root |= ryml::MAP;
        root["schemaVersion"] << 1;
        root["source"] << "product-live";
        root["backend"] << (backend == EnhancedLiveBackend::DX12 ? "dx12" : "vulkan");
        root["frameId"] << frame.frameId;
        root["sourceRealFrameId"] << capturedRealFrameId;
        root["requestedAfterFrameId"] << afterFrameId;
        root["sceneEpoch"] << frame.sceneEpoch;
        root["viewId"] << view.key.viewId;
        root["historyRevision"] << view.key.historyRevision;
        root["width"] << frame.width;
        root["height"] << frame.height;
        root["totalSeconds"] << frame.totalSeconds;
        root["deltaSeconds"] << frame.deltaSeconds;
        root["captureMode"] << (controlled
            ? (cameraReplay && (frame.totalSeconds != 0.f || frame.deltaSeconds != 0.f)
                ? "camera-clock-replay-v1" : "static-repeatability-v1") : "observation");
        root["skyBoxEnabled"] << frame.skyBoxEnabled;
        root["viewFlags"] << static_cast<uint32_t>(view.viewFlags);
        root["cameraInputContract"] << "camera-clock-v1";
        root["cameraInputReplayed"] << cameraReplay.has_value();
        root["drawInputContract"] << "selected-transform-pose-v1";
        root["drawInputReplayed"] << drawReplay.has_value();
        if (!drawInputBytes.empty()) root["drawInputFile"] << "draw-input.bin";
        root["drawReplayExtensionSelected"] << replayExtensions;
        root["latticeReplayExtensionSelected"] << latticeReplayExtension;
        root["latticeReplayExtensionExecuted"] << !latticeInputBytes.empty();
        if (latticeReplayExtension) root["latticeInputContract"] << "lattice-instance-v1";
        root["latticeInputReplayed"] << latticeReplay.has_value();
        if (!latticeInputBytes.empty()) root["latticeInputFile"] << "lattice-input.bin";
        // Preview uses a separate host contract and is deliberately unsupported.
        if (view.displayTarget != EnhancedLiveDisplayTarget::MaterialPreview)
        {
            EnhancedCameraReplayInput saved;
            saved.width = frame.width; saved.height = frame.height;
            saved.target = static_cast<uint32_t>(view.displayTarget);
            saved.viewFlags = static_cast<uint32_t>(view.viewFlags);
            saved.skyBoxEnabled = frame.skyBoxEnabled;
            saved.totalSeconds = frame.totalSeconds; saved.deltaSeconds = frame.deltaSeconds;
            saved.camera = view.camera;
            cameraInputBytes = saved.Encode();
            EnhancedCameraReplayInput checked;
            std::string error;
            if (!EnhancedCameraReplayInput::Decode(cameraInputBytes, checked, error))
                throw std::runtime_error(error);
            root["cameraInputFile"] << "camera-input.bin";
        }
        if (controlled)
        {
            root["sampleIndex"] << 0;
            root["historyPolicy"] << "restart-ssgi-fog";
        }
        const auto matrix = [](ryml::NodeRef node, const math::matrix4x4& value)
        {
            std::array<float, 16> values;
            static_assert(sizeof(value) == sizeof(values));
            std::memcpy(values.data(), &value, sizeof(value));
            node |= ryml::SEQ;
            for (float component : values) node.append_child() << component;
        };
        root["camera"] |= ryml::MAP;
        matrix(root["camera"]["view"], view.camera.view);
        matrix(root["camera"]["projection"], view.camera.projection);
        root["skyBoxPath"] << skyBoxPath;
        root["lights"] |= ryml::SEQ;
        for (const auto& light : lights)
        {
            // EnhancedLight has an asserted packed 16-float layout.
            std::array<float, 16> values;
            std::memcpy(values.data(), &light, sizeof(light));
            auto node = root["lights"].append_child();
            node |= ryml::SEQ;
            for (float component : values) node.append_child() << component;
        }
        root["draws"] |= ryml::SEQ;
        const auto append = [&](const EnhancedDrawItem& draw, const auto& material,
            const char* route)
        {
            auto item = root["draws"].append_child();
            item |= ryml::MAP;
            item["route"] << route;
            matrix(item["world"], draw.worldMatrix);
            WriteMeshIdentity(item, draw.modelMeshView.handle);
            if (material)
            {
                // W8: 이 packet이 어느 저작 값·어느 프레임의 밀봉인지.
                auto seal = item["seal"];
                seal |= ryml::MAP;
                seal["hash"] << material->seal.sealHash;
                seal["authoredDigest"] << material->seal.authoredDigest;
                seal["authoredRevision"] << material->seal.authoredRevision;
                seal["modelGeneration"] << material->seal.modelGeneration;
                seal["sceneEpoch"] << material->seal.sceneEpoch;
                seal["frameId"] << material->seal.frameId;
                seal["stamped"] << material->seal.IsStamped();
                item["shaderMetaSlot"] << material->shaderMetaHandle.slot;
                item["shaderMetaGeneration"] << material->shaderMetaHandle.generation;
                item["permutation"] << material->permutationKey.Hex();
                item["propertyBytes"] |= ryml::SEQ;
                for (auto byte : material->propertyBytes)
                    item["propertyBytes"].append_child() << static_cast<uint32_t>(byte);
                item["useNormalMap"] << material->useNormalMap;
                item["coverageFlags"] << material->coverage.flags;
                item["alphaCutoff"] << material->coverage.cutoff;
                item["textures"] |= ryml::SEQ;
                for (const auto& texture : material->textureBindings)
                {
                    auto binding = item["textures"].append_child();
                    binding |= ryml::MAP;
                    binding["property"] << texture.propertyName;
                    binding["assetId"] << texture.textureGuid.ToString();
                    binding["register"] << texture.registerIndex;
                    binding["space"] << texture.registerSpace;
                    binding["runtimeIdentity"] << (texture.textureOwner
                        ? texture.textureOwner->m_assetId.m_ID_Data : 0);
                    binding["authored"] << (texture.textureOwner ? "true" : "false");
                }
            }
            else item["missing"] << "material snapshot";
        };
        for (const auto& draw : opaque) append(draw, draw.materialSnapshot, "gbuffer");
        for (const auto& draw : transparent) append(draw, draw.forwardMaterialSnapshot, "forward");
        // W8 이전에는 여기서 sampler identity·descriptor generation·resolved PSO
        // key를 "없다"고 적었다. 이제 그 셋은 패스가 배치를 확정한 뒤에 알 수
        // 있으므로 Save 직전 RecordSealLedger가 채운다. 기록이 없는 채로 저장되면
        // 그것 자체가 배선 결함이므로 기본값을 "미기록"으로 둔다.
        root["sealLedger"] |= ryml::MAP;
        root["sealLedger"]["recorded"] << false;
    }

    void WriteTemporalProvenance(ryml::NodeRef node) const
    {
        const auto& provenance = temporalProvenance;
        node["temporalProvenanceSchemaVersion"] << 2;
        node["frameKind"] << TemporalMeasuredFrameKindName(provenance.frameKind);
        node["realFrameId"] << provenance.realFrameId;
        node["generatedOrdinal"] << provenance.generatedOrdinal;
        node["renderWidth"] << provenance.renderExtent.width;
        node["renderHeight"] << provenance.renderExtent.height;
        node["displayWidth"] << provenance.displayExtent.width;
        node["displayHeight"] << provenance.displayExtent.height;
        node["upscaler"] << TemporalMeasuredProviderName(provenance.upscaler);
        node["frameGenerator"] << TemporalMeasuredProviderName(provenance.frameGenerator);
        node["resolutionState"] << TemporalResolutionStateName(provenance.resolutionState);
        node["spatialMode"] << SpatialScalingModeName(provenance.spatialMode);
        node["deepDvcApplied"] << provenance.deepDvcApplied;
        node["temporalNativeGateActive"] << provenance.nativeGateActive;
    }

    void RecordTemporalProvenance(const TemporalMeasurementProvenance& provenance)
    {
        temporalProvenance = provenance;
        auto root = manifest.rootref();
        WriteTemporalProvenance(root);
        root["goldenEligible"] << provenance.IsGoldenEligible();
        // Graph timing is collected before submission. Refresh its frame/effect
        // labels when Commit supplies the successfully submitted provenance.
        if (root.has_child("measurement"))
        {
            WriteTemporalProvenance(root["measurement"]);
        }
    }

    void RecordLatticeInput(const own::shared_owner<const material_graph::SceneViewInput>& input)
    {
        if (!input)
        {
            return;
        }
        auto root = manifest.rootref();
        for (const auto& draw : input->Draws())
        {
            auto item = root["draws"].append_child();
            item |= ryml::MAP;
            item["route"] << "lattice";
            const auto& geometry = draw.geometry->Source()->Geometry();
            WriteMeshIdentity(item, geometry.handle);
            item["world"] |= ryml::SEQ;
            std::array<float, 16> world;
            std::memcpy(world.data(), &draw.geometry->Source()->World(), sizeof(world));
            for (float value : world) item["world"].append_child() << value;
            item["pose"] |= ryml::SEQ;
            for (const auto& bone : draw.geometry->Source()->Bones())
            {
                const auto* bytes = reinterpret_cast<const unsigned char*>(&bone);
                for (size_t i = 0; i < sizeof(bone); ++i)
                    item["pose"].append_child() << static_cast<uint32_t>(bytes[i]);
            }
            const auto& instance = *draw.material;
            auto graph = item["lattice"];
            graph |= ryml::MAP;
            graph["graphId"] << FileGuid(instance.generation->assetId.value).ToString();
            graph["generation"] << instance.generation->generation;
            graph["slot"] << draw.materialSlot;
            graph["features"] << instance.generation->cooked.product.program.features;
            graph["coverage"] << static_cast<uint32_t>(draw.queue);
            graph["uniformBytes"] |= ryml::SEQ;
            for (auto byte : instance.uniforms)
            {
                graph["uniformBytes"].append_child() << static_cast<uint32_t>(byte);
            }
            graph["textures"] |= ryml::SEQ;
            for (const auto& texture : instance.textures)
            {
                auto binding = graph["textures"].append_child();
                binding |= ryml::MAP;
                binding["slot"] << texture.slot;
                binding["assetId"] << FileGuid(texture.assetId.value).ToString();
                binding["colorSpace"] << static_cast<uint32_t>(texture.colorSpace);
                binding["authored"] << !!texture.owner;
            }
        }
    }

    // 패스가 이번 프레임의 배치를 확정한 뒤에 부른다. draw snapshot만으로는
    // 알 수 없는 축(어느 PSO로 그렸는가, 어떤 sampler를 걸었는가, descriptor
    // 배치가 갈리지 않았는가)을 여기서 적는다.
    void RecordSealLedger(const EnhancedDrawSealLedger& gbuffer,
        std::uint64_t gbufferSampler,
        const EnhancedDrawSealLedger& forward, std::uint64_t forwardSampler,
        std::uint64_t encoderDrops, const std::string& lastEncoderDrop,
        std::uint32_t textureUploadFailures)
    {
        auto root = manifest.rootref();
        auto ledger = root["sealLedger"];
        ledger |= ryml::MAP;
        ledger["recorded"] << true;
        ledger["encoderDrops"] << encoderDrops;
        ledger["lastEncoderDrop"] << lastEncoderDrop;
        // W9 — 업로드 실패를 흰색으로 덮은 횟수. 저작으로 없는 슬롯의 중립값은
        // 여기 들어가지 않는다. 0이 아니면 화면의 흰색을 재질로 읽으면 안 된다.
        ledger["textureUploadFailures"] << textureUploadFailures;
        const auto appendPass = [&](const char* name,
            const EnhancedDrawSealLedger& source, std::uint64_t samplerIdentity)
        {
            auto node = ledger[ryml::to_csubstr(name)];
            node |= ryml::MAP;
            node["frameId"] << source.frameId;
            node["sceneEpoch"] << source.sceneEpoch;
            node["samplerIdentity"] << samplerIdentity;
            node["stamped"] << source.counters.stamped;
            node["unstamped"] << source.counters.unstamped;
            node["staleFrame"] << source.counters.staleFrame;
            node["valueMismatch"] << source.counters.valueMismatch;
            node["pipelineConflict"] << source.counters.pipelineConflict;
            node["bindingConflict"] << source.counters.bindingConflict;
            node["skipped"] << source.counters.skipped;
            node["dropped"] << source.recordDrops.Total();
            node["violations"] << source.Violations();
            node["lastReason"] << source.lastReason;
            auto bindings = node["bindings"];
            bindings |= ryml::SEQ;
            for (const auto& [sealHash, binding] : source.bindings)
            {
                auto entry = bindings.append_child();
                entry |= ryml::MAP;
                entry["sealHash"] << sealHash;
                entry["pipelineId"] << binding.pipelineId;
                entry["textureDigest"] << binding.textureDigest;
                entry["samplerIdentity"] << binding.samplerIdentity;
                // W0 — 이 셋이 draw 별 신원의 세 축이다. draw 쪽 `seal.hash` 로
                // 여기 `sealHash` 를 조인하면 draw 마다의 PSO·sampler·descriptor
                // 버전이 나온다. 값을 여기 두는 이유는 같은 밀봉을 공유하는
                // draw 들이 **같은 바인딩을 써야 한다**는 것이 W8 의 불변식이라,
                // draw 마다 복사하면 그 불변식이 표에서 안 보이게 되기 때문이다.
                entry["descriptorVersion"] << binding.descriptorVersion;
            }
        };
        appendPass("gbuffer", gbuffer, gbufferSampler);
        appendPass("forward", forward, forwardSampler);
    }

    bool RecordCompiledGraph(const EnhancedRenderGraph& graph, double recordMs, double compileMs)
    {
        EnhancedRenderGraph::DiagnosticSnapshot snapshot;
        if (!graph.CaptureDiagnosticSnapshot(snapshot)) return false;
        auto root = manifest.rootref();
        auto node = root["compiledGraph"];
        node |= ryml::MAP;
        node["schemaVersion"] << 3;
        node["generation"] << snapshot.generation;
        node["graphEpoch"] << snapshot.graphEpoch;
        node["dependencyHash"] << snapshot.dependencyHash;
        node["orderContract"] << (snapshot.scheduling == RGSchedulingMode::DeclarationOrder
            ? "legacy-declaration-order" : snapshot.orderPolicy == RGOrderPolicy::DependencyOrder
                ? "dependency-order" : "preserve-declaration-order");
        node["accessContract"] << (snapshot.scheduling == RGSchedulingMode::DeclarationOrder
            ? "inferred-from-state" : "explicit-access");
        node["versionsSupported"] << (snapshot.scheduling == RGSchedulingMode::ExplicitVersioned);
        // The executed plan is authoritative, including graphics-only fallback.
        // Never reconstruct boundaries from the requested queue mode.
        const auto& diagnostics = graph.GetQueueDiagnostics();
        auto queue = node["queueExecution"];
        queue |= ryml::MAP;
        queue["schemaVersion"] << 2;
        queue["mode"] << diagnostics.requestedExecutionMode;
        queue["requestedExecutionMode"] << diagnostics.requestedExecutionMode;
        queue["effectiveExecutionMode"] << diagnostics.effectiveExecutionMode;
        queue["measurementDomain"] << RGMeasurementDomainName(graph.GetMeasurementDomain());
        queue["fallbackReason"] << RGQueueFallbackReasonName(diagnostics.schedule.fallbackReason);
        queue["specialized"] << diagnostics.specialized;
        queue["predictionCalibrated"] << false;
        auto execution = queue["execution"];
        execution |= ryml::MAP;
        execution["scheduleMilliseconds"] << diagnostics.execution.scheduleMilliseconds;
        execution["recordingMilliseconds"] << diagnostics.execution.recordingMilliseconds;
        execution["submissionMilliseconds"] << diagnostics.execution.submissionMilliseconds;
        execution["totalMilliseconds"] << diagnostics.execution.totalMilliseconds;
        execution["recordedLists"] << graph.GetStats().recordedLists;
        execution["recordWorkers"] << graph.GetStats().recordWorkers;
        execution["recordUnits"] << graph.GetStats().recordUnits;
        execution["recordingWaveCount"] << graph.GetStats().recordingWaveCount;
        execution["plannedBatches"] << diagnostics.execution.plannedBatches;
        execution["plannedComputeBatches"] << diagnostics.execution.plannedComputeBatches;
        execution["plannedWaits"] << diagnostics.execution.plannedWaits;
        execution["submittedBatches"] << diagnostics.execution.submittedBatches;
        execution["computeBatches"] << diagnostics.execution.computeBatches;
        execution["submittedWaits"] << diagnostics.execution.submittedWaits;
        execution["plannedBarriers"] << diagnostics.execution.plannedBarriers;
        execution["prologueBarriers"] << diagnostics.execution.prologueBarriers;
        execution["epilogueBarriers"] << diagnostics.execution.epilogueBarriers;
        execution["predictedSerialNanoseconds"] << diagnostics.execution.predictedSerialNanoseconds;
        execution["predictedNanoseconds"] << diagnostics.execution.predictedNanoseconds;
        execution["submissionAttempted"] << diagnostics.execution.submissionAttempted;
        execution["recoveryRequired"] << diagnostics.execution.recoveryRequired;
        execution["completed"] << diagnostics.execution.completed;
        auto schedule = queue["schedule"];
        schedule |= ryml::MAP;
        schedule["compileGeneration"] << diagnostics.schedule.compileGeneration;
        schedule["usesCompute"] << diagnostics.schedule.usesCompute;
        schedule["predictionComplete"] << diagnostics.schedule.predictionComplete;
        schedule["predictionCalibrated"] << diagnostics.schedule.predictionCalibrated;
        schedule["fallbackReason"] << RGQueueFallbackReasonName(diagnostics.schedule.fallbackReason);
        schedule["eligibleComputePasses"] << diagnostics.schedule.eligibleComputePasses;
        schedule["rejectedStatePasses"] << diagnostics.schedule.rejectedStatePasses;
        schedule["measuredPasses"] << diagnostics.schedule.measuredPasses;
        schedule["missingMeasurementPasses"] << diagnostics.schedule.missingMeasurementPasses;
        schedule["entries"] |= ryml::SEQ;
        for (const auto& entry : diagnostics.schedule.entries)
        {
            auto item = schedule["entries"].append_child();
            item |= ryml::MAP;
            item["pass"] << entry.pass;
            item["queue"] << static_cast<uint32_t>(entry.queue);
        }
        schedule["waits"] |= ryml::SEQ;
        for (const auto& wait : diagnostics.schedule.waits)
        {
            auto item = schedule["waits"].append_child();
            item |= ryml::MAP;
            item["producer"] << wait.producer;
            item["consumer"] << wait.consumer;
            item["resource"] << wait.resource;
        }
        queue["batches"] |= ryml::SEQ;
        for (const auto& batch : diagnostics.batches)
        {
            auto item = queue["batches"].append_child();
            item |= ryml::MAP;
            item["queue"] << static_cast<uint32_t>(batch.queue);
            item["barriers"] << batch.barriers;
            item["submitted"] << batch.submitted;
            item["passes"] |= ryml::SEQ;
            for (const auto pass : batch.passes)
            {
                item["passes"].append_child() << pass;
            }
        }
        queue["waits"] |= ryml::SEQ;
        for (const auto& wait : diagnostics.waits)
        {
            auto item = queue["waits"].append_child();
            item |= ryml::MAP;
            item["producerBatch"] << wait.producerBatch;
            item["consumerBatch"] << wait.consumerBatch;
            item["submitted"] << wait.submitted;
        }
        node["reachabilityEdges"] |= ryml::SEQ;
        for (const auto& edge : snapshot.reachabilityEdges)
        {
            auto item = node["reachabilityEdges"].append_child();
            item |= ryml::MAP;
            item["producer"] << edge.producer;
            item["consumer"] << edge.consumer;
            item["resource"] << edge.resource;
        }
        node["versionEdges"] |= ryml::SEQ;
        for (const auto& edge : snapshot.versionEdges)
        {
            auto item = node["versionEdges"].append_child();
            item |= ryml::MAP;
            item["producer"] << edge.producer;
            item["consumer"] << edge.consumer;
            item["resource"] << edge.resource;
            item["version"] << edge.version;
            item["reason"] << static_cast<uint32_t>(edge.reason);
        }
        node["dependencyWaves"] |= ryml::SEQ;
        for (const auto wave : snapshot.dependencyWaves)
        {
            node["dependencyWaves"].append_child() << wave;
        }
        node["criticalPath"] |= ryml::SEQ;
        for (const auto index : snapshot.criticalPath)
        {
            node["criticalPath"].append_child() << index;
        }
        node["executeOrder"] |= ryml::SEQ;
        for (auto index : snapshot.executeOrder) node["executeOrder"].append_child() << index;
        node["resources"] |= ryml::SEQ;
        for (uint32_t i = 0; i < snapshot.resources.size(); ++i)
        {
            const auto& resource = snapshot.resources[i];
            auto entry = node["resources"].append_child();
            entry |= ryml::MAP;
            entry["id"] << i;
            entry["name"] << resource.name;
            entry["imported"] << resource.imported;
            entry["kind"] << (resource.buffer ? "buffer" : "texture");
            entry["used"] << resource.used;
            entry["firstUse"] << resource.firstUse;
            entry["lastUse"] << resource.lastUse;
            entry["versionCount"] << resource.versionCount;
            entry["initialState"] << static_cast<uint32_t>(resource.initialState);
            entry["finalState"] << static_cast<uint32_t>(resource.finalState);
            entry["aliasGroup"] << resource.aliasGroup;
            entry["allocationBytes"] << resource.allocationBytes;
        }
        node["passes"] |= ryml::SEQ;
        for (const auto& pass : snapshot.passes)
        {
            auto entry = node["passes"].append_child();
            entry |= ryml::MAP;
            entry["name"] << pass.name;
            entry["authoredIndex"] << pass.authoredIndex;
            entry["compiledIndex"] << pass.compiledIndex;
            entry["culled"] << pass.culled;
            entry["sideEffect"] << pass.sideEffect;
            entry["recordCost"] << pass.recordCost;
            entry["maxSlices"] << pass.maxSlices;
            entry["repeatCount"] << pass.repeatCount;
            entry["phases"] |= ryml::SEQ;
            for (const auto& phase : pass.phases)
            {
                auto phaseEntry = entry["phases"].append_child();
                phaseEntry |= ryml::MAP;
                phaseEntry["name"] << phase.name;
                phaseEntry["usages"] |= ryml::SEQ;
                for (const auto& usage : phase.usages)
                {
                    auto item = phaseEntry["usages"].append_child();
                    item |= ryml::MAP;
                    item["resource"] << usage.resource;
                    item["state"] << static_cast<uint32_t>(usage.state);
                    item["access"] << static_cast<uint32_t>(usage.access);
                    item["version"] << usage.version;
                    item["kind"] << static_cast<uint32_t>(usage.kind);
                }
                const auto appendPhaseBarriers = [&](const char* name, const auto& barriers)
                {
                    phaseEntry[name] |= ryml::SEQ;
                    for (const auto& barrier : barriers)
                    {
                        auto item = phaseEntry[name].append_child();
                        item |= ryml::MAP;
                        item["resource"] << barrier.resource;
                        item["before"] << static_cast<uint32_t>(barrier.before);
                        item["after"] << static_cast<uint32_t>(barrier.after);
                        item["uav"] << barrier.uav;
                        item["afterPass"] << barrier.afterPass;
                    }
                };
                appendPhaseBarriers("firstBarriers", phase.firstBarriers);
                appendPhaseBarriers("repeatBarriers", phase.repeatBarriers);
            }
            entry["usages"] |= ryml::SEQ;
            for (const auto& usage : pass.usages)
            {
                auto item = entry["usages"].append_child();
                item |= ryml::MAP;
                item["resource"] << usage.resource;
                item["state"] << static_cast<uint32_t>(usage.state);
                item["inferredWrite"] << usage.inferredWrite;
                item["access"] << static_cast<uint32_t>(usage.access);
                item["version"] << usage.version;
                item["kind"] << static_cast<uint32_t>(usage.kind);
            }
            entry["barriers"] |= ryml::SEQ;
            for (const auto& barrier : pass.barriers)
            {
                auto item = entry["barriers"].append_child();
                item |= ryml::MAP;
                item["resource"] << barrier.resource;
                item["before"] << static_cast<uint32_t>(barrier.before);
                item["after"] << static_cast<uint32_t>(barrier.after);
                item["uav"] << barrier.uav;
                item["afterPass"] << barrier.afterPass;
                item["aliasing"] << barrier.aliasing;
            }
        }
        root["measurement"] |= ryml::MAP;
        // All timing/memory values below belong to this submitted real frame,
        // never to a generated presentation or a newer status snapshot.
        WriteTemporalProvenance(root["measurement"]);
        root["measurement"]["cpuRecordMs"] << recordMs;
        root["measurement"]["cpuGraphCompileMs"] << compileMs;
        root["measurement"]["cpuTransientPrepareMs"] << graph.GetStats().transientPrepareCpuMs;
        root["measurement"]["transientPrepareScope"] << "lifetime-discovery-allocation-cache-resource-creation";
        const auto heapMemory = graph.GetAliasHeapMemory();
        auto heaps = root["measurement"]["aliasHeapMemory"];
        heaps |= ryml::MAP;
        heaps["scope"] << "unique-group-owned-native-heaps-not-resident";
        heaps["domainId"] << heapMemory.domainId;
        heaps["retainedBytes"] << heapMemory.retainedBytes;
        heaps["cachedBytes"] << heapMemory.cachedBytes;
        heaps["leasedBytes"] << heapMemory.leasedBytes;
        heaps["peakRetainedBytes"] << heapMemory.peakRetainedBytes;
        heaps["retainedHeaps"] << heapMemory.retainedHeaps;
        heaps["cachedHeaps"] << heapMemory.cachedHeaps;
        heaps["leasedHeaps"] << heapMemory.leasedHeaps;
        root["measurement"]["scope"] << "capture-submission-including-readbacks";
        root["measurement"]["domain"] << RGMeasurementDomainName(graph.GetMeasurementDomain());
        root["measurement"]["normalFrameComparable"] << false;
        root["measurement"]["gpuStatus"] << "unsupported";
        root["measurement"]["gpuReason"] << "backend has no product pass timestamp collector";
        return true;
    }

    void RecordSubmissionCpu(double recordAndSubmitMs, double submissionMs)
    {
        auto node = manifest.rootref()["measurement"];
        node["cpuRecordSubmitMs"] << recordAndSubmitMs;
        node["cpuSubmitMs"] << submissionMs;
        node["cpuRecordSubmitScope"] << "record-schedule-submit-frame-join";
    }

    void RecordIblContract(uint32_t baseSamples, uint32_t reflectionSamples, const RHITextureInfo& info)
    {
        auto node = manifest.rootref()["ibl"];
        node |= ryml::MAP;
        node["baseSamples"] << baseSamples;
        node["reflectionSamples"] << reflectionSamples;
        node["proposalWidth"] << info.width;
        node["proposalHeight"] << info.height;
    }

    void RecordMemory(const RHIVideoMemoryInfo& memory)
    {
        auto node = manifest.rootref()["measurement"]["memory"];
        node |= ryml::MAP;
        node["scope"] << "device-budget-snapshot-not-transient-peak";
        node["available"] << (memory.usageAvailable && memory.budgetAvailable);
        node["usageAvailable"] << memory.usageAvailable;
        node["budgetAvailable"] << memory.budgetAvailable;
        node["usedBytes"] << memory.usedBytes;
        node["budgetBytes"] << memory.budgetBytes;
        node["usedMB"] << memory.usedMB;
        node["budgetMB"] << memory.budgetMB;
    }

    void RecordGpuTiming(const std::vector<EnhancedLivePassTiming>& passes,
        const EnhancedLiveGpuSpan& span, double totalMs, const std::string& error)
    {
        auto node = manifest.rootref()["measurement"];
        node["gpuStatus"] << (error.empty() ? "measured" : "failed");
        node["gpuReason"] << error;
        node["gpuTotalMs"] << totalMs;
        node["gpuQueueSpanMs"] << span.queueSpanMs;
        node["gpuBusyMs"] << span.busyMs;
        node["queryOverflow"] << span.queryOverflowPasses;
        node["droppedSlices"] << span.droppedSlices;
        node["sliceCount"] << span.sliceCount;
        node["computeSliceCount"] << span.computeSliceCount;
        node["clockValid"] << span.cpuAligned;
        node["overlapClockValid"] << span.overlapClockValid;
        node["cpuTicksPerSecond"] << span.cpuTicksPerSecond;
        node["measuredOverlapMilliseconds"] << span.measuredOverlapMilliseconds;
        node["overlapClockErrorMilliseconds"] << span.overlapClockErrorMilliseconds;
        node["overlapScope"] << "calibrated-queue-local-pass-interval-intersection";
        node["passes"] |= ryml::SEQ;
        for (const auto& pass : passes)
        {
            auto item = node["passes"].append_child();
            item |= ryml::MAP;
            item["name"] << pass.name;
            item["milliseconds"] << pass.milliseconds;
            item["spanMilliseconds"] << pass.spanMilliseconds;
        }
    }

    bool Declare(IRenderDeviceServices& resources, EnhancedRenderGraph& graph,
        const LiveBlackboard& blackboard, uint32_t width, uint32_t height,
        std::string& error)
    {
        graph.SetMeasurementDomain(RGMeasurementDomain::Capture);
        constexpr const char* slots[] = { LiveSlots::kGBufferDiffuse,
            LiveSlots::kGBufferMetalRough, LiveSlots::kGBufferNormal,
            LiveSlots::kGBufferEmissive, LiveSlots::kGBufferDepth,
            LiveSlots::kLitColor, LiveSlots::kDisplayLdr };
        for (uint32_t i = 0; i < readbacks.size(); ++i)
        {
            const auto handle = blackboard.Get(slots[i]);
            if (!handle.IsValid()) { error = std::string("missing capture output: ") + slots[i]; return false; }
            const auto format = i < 4 ? EnhancedGBufferPass::GetRenderTargetFormat(i)
                : i == 4 ? RHIFormat::D32Float
                : i == 5 ? RHIFormat::RGBA16Float : RHIFormat::RGBA8Unorm;
            if (!resources.CreateReadback(width, height, format, 1, readbacks[i], error))
                return false;
            const auto readback = readbacks[i];
            graph.AddPass(std::string("PBR.Capture.") + slots[i],
                { { handle, RHIResourceState::CopySource,
                graph.GetSchedulingMode() != RGSchedulingMode::DeclarationOrder
                    ? RGAccessMode::Read : RGAccessMode::LegacyState } },
                [handle, readback](const EnhancedRenderGraph::ExecuteContext& context)
                { context.encoder->CopyToReadback(readback, context.ResolveHandle(handle)); }, true);
        }
        return true;
    }

    bool Save(IRenderDeviceServices& resources, const EnhancedRenderGraph::Stats& stats,
        std::string& error, uint32_t validationCount, const std::string& validation)
    {
        if (!temporalProvenance.IsGoldenEligible() || temporalProvenance.realFrameId != capturedRealFrameId ||
            temporalProvenance.publicationFrameId != result.frameId ||
            temporalProvenance.viewId != capturedViewId || temporalProvenance.sceneEpoch != capturedSceneEpoch)
        {
            error = "capture requires observed native-only real-frame provenance for this frame";
            Fail(error);
            return false;
        }
        try
        {
            const std::filesystem::path root(result.directory);
            constexpr const char* names[] = { "baseColor", "metalRough", "normal",
                "emissive", "depth", "preToneHdr", "display" };
            auto rootNode = manifest.rootref();
            rootNode["validationCount"] << validationCount;
            rootNode["validation"] << validation;
            rootNode["attachments"] |= ryml::SEQ;
            bool finite = true;
            for (uint32_t i = 0; i < readbacks.size(); ++i)
            {
                RHIReadbackImage image;
                if (!resources.MapReadback(readbacks[i], image, error)) return false;
                const uint32_t channels = i == 4 ? 1 : 4;
                std::vector<float> pixels;
                pixels.reserve(static_cast<size_t>(image.width) * image.height * channels);
                float minimum = std::numeric_limits<float>::max();
                float maximum = std::numeric_limits<float>::lowest();
                float rgbMaximum = std::numeric_limits<float>::lowest();
                uint64_t nonfinite = 0;
                for (uint32_t y = 0; y < image.height; ++y)
                    for (uint32_t x = 0; x < image.width; ++x)
                        for (uint32_t c = 0; c < channels; ++c)
                        {
                            const float value = image.At(x, y, c);
                            pixels.push_back(value);
                            if (!std::isfinite(value)) { ++nonfinite; continue; }
                            minimum = (std::min)(minimum, value);
                            maximum = (std::max)(maximum, value);
                            if (c < 3) rgbMaximum = (std::max)(rgbMaximum, value);
                        }
                const std::string file = std::string(names[i]) + ".f32";
                std::ofstream output(root / file, std::ios::binary | std::ios::trunc);
                output.write(reinterpret_cast<const char*>(pixels.data()),
                    static_cast<std::streamsize>(pixels.size() * sizeof(float)));
                output.close();
                if (!output) { error = "capture write failed: " + file; return false; }
                auto attachment = rootNode["attachments"].append_child();
                attachment |= ryml::MAP;
                attachment["name"] << names[i];
                attachment["file"] << file;
                attachment["encoding"] << "float32-le-row-major";
                attachment["channels"] << channels;
                attachment["width"] << image.width;
                attachment["height"] << image.height;
                attachment["frameKind"] << TemporalMeasuredFrameKindName(temporalProvenance.frameKind);
                attachment["realFrameId"] << temporalProvenance.realFrameId;
                attachment["generatedOrdinal"] << temporalProvenance.generatedOrdinal;
                attachment["temporalProvenanceSchemaVersion"] << 2;
                attachment["spatialMode"] << SpatialScalingModeName(temporalProvenance.spatialMode);
                attachment["deepDvcApplied"] << temporalProvenance.deepDvcApplied;
                attachment["nonfinite"] << nonfinite;
                attachment["min"] << minimum;
                attachment["max"] << maximum;
                attachment["rgbMax"] << rgbMaximum;
                finite &= nonfinite == 0;
            }
            struct StageImage
            {
                uint32_t width{}, height{};
                uint64_t nonfinite{};
            };
            std::vector<StageImage> stageImages;
            stageImages.reserve(stageReadbacks.size());
            for (const auto& stage : stageReadbacks)
            {
                RHIReadbackImage image;
                if (!resources.MapReadback(stage.readback, image, error))
                {
                    return false;
                }
                std::vector<float> pixels;
                pixels.reserve(static_cast<size_t>(image.width) * image.height * 4);
                uint64_t nonfinite = 0;
                for (uint32_t y = 0; y < image.height; ++y)
                {
                    for (uint32_t x = 0; x < image.width; ++x)
                    {
                        for (uint32_t c = 0; c < 4; ++c)
                        {
                            const float value = image.At(x, y, c);
                            pixels.push_back(value);
                            nonfinite += !std::isfinite(value);
                        }
                    }
                }
                const auto file = stage.name + ".f32";
                std::ofstream output(root / file, std::ios::binary | std::ios::trunc);
                output.write(reinterpret_cast<const char*>(pixels.data()),
                    static_cast<std::streamsize>(pixels.size() * sizeof(float)));
                output.close();
                if (!output)
                {
                    error = "stage capture write failed: " + file;
                    return false;
                }
                stageImages.push_back({image.width, image.height, nonfinite});
                finite &= nonfinite == 0;
            }
            rootNode["diagnosticStageReadbackCount"] << stageReadbacks.size();
            rootNode["diagnosticStages"] |= ryml::SEQ;
            for (const auto& stage : stages)
            {
                const auto& readback = stageReadbacks[stage.readbackIndex];
                const auto& image = stageImages[stage.readbackIndex];
                auto item = rootNode["diagnosticStages"].append_child();
                item |= ryml::MAP;
                item["name"] << stage.name;
                item["file"] << readback.name + ".f32";
                item["readbackStage"] << readback.name;
                item["sharedReadback"] << (stage.name != readback.name);
                item["resource"] << readback.source.index;
                item["version"] << readback.source.version;
                item["kind"] << static_cast<uint32_t>(readback.source.kind);
                item["graphEpoch"] << readback.source.epoch;
                item["encoding"] << "float32-le-row-major";
                item["width"] << image.width;
                item["height"] << image.height;
                item["channels"] << 4;
                item["nonfinite"] << image.nonfinite;
            }
            auto graphNode = rootNode["graph"];
            graphNode |= ryml::MAP;
            graphNode["declared"] << stats.passesDeclared;
            graphNode["culled"] << stats.passesCulled;
            graphNode["executed"] << stats.passesExecuted;
            graphNode["barriers"] << stats.barriersEmitted;
            graphNode["transientAllocationQueries"] << stats.transientAllocationQueries;
            graphNode["aliasHeapCreates"] << stats.aliasHeapCreates;
            graphNode["aliasHeapReuses"] << stats.aliasHeapReuses;
            graphNode["aliasResourceReuses"] << stats.aliasResourceReuses;
            graphNode["poisonInitializations"] << stats.poisonInitializations;
            graphNode["transientPrepareCpuMs"] << stats.transientPrepareCpuMs;
            rootNode["finite"] << (finite ? "true" : "false");
            std::ofstream output(root / "manifest.json", std::ios::trunc);
            output << ryml::emitrs_json<std::string>(manifest) << '\n';
            output.close();
            if (!output) { error = "capture manifest write failed"; return false; }
            if (!finite) { error = "capture contains nonfinite pixels"; return false; }
            if (validationCount != 0) { error = "capture contains GPU validation messages: " + validation; return false; }
            if (!cameraInputBytes.empty())
            {
                std::ofstream packet(root / "camera-input.bin", std::ios::binary | std::ios::trunc);
                packet.write(reinterpret_cast<const char*>(cameraInputBytes.data()), cameraInputBytes.size());
                packet.close();
                if (!packet) { error = "camera replay input write failed"; return false; }
            }
            if (!drawInputBytes.empty())
            {
                std::ofstream packet(root / "draw-input.bin", std::ios::binary | std::ios::trunc);
                packet.write(reinterpret_cast<const char*>(drawInputBytes.data()), drawInputBytes.size());
                packet.close();
                if (!packet) { error = "draw replay input write failed"; return false; }
            }
            if (!latticeInputBytes.empty())
            {
                std::ofstream packet(root / "lattice-input.bin", std::ios::binary | std::ios::trunc);
                packet.write(reinterpret_cast<const char*>(latticeInputBytes.data()), latticeInputBytes.size());
                packet.close();
                if (!packet) { error = "Lattice replay input write failed"; return false; }
            }
            result.state = EnhancedPbrCaptureState::Complete;
            nativeExclusion.reset();
            return true;
        }
        catch (const std::exception& exception) { error = exception.what(); return false; }
    }

    bool HasResources() const
    {
        for (const auto& readback : readbacks)
        {
            if (readback.IsValid())
            {
                return true;
            }
        }
        return !stageReadbacks.empty();
    }

    void Release(IRenderDeviceServices& resources)
    {
        for (auto& readback : readbacks)
        {
            resources.ReleaseReadback(readback);
        }
        for (auto& stage : stageReadbacks)
        {
            resources.ReleaseReadback(stage.readback);
        }
        stageReadbacks.clear();
        stages.clear();
    }
    void Fail(const std::string& error)
    {
        result.state = EnhancedPbrCaptureState::Failed;
        result.error = error;
        nativeExclusion.reset();
    }
};
