#include "../Containers/ContainerAPI.h"
#pragma once
#include "../Utility_Framework/Foundation/EngineOrderedMap.h"
#if defined(CE_CONTRACT_STANDALONE)
#include "../../Tools/build/ModuleAdapters/Creator.Core.Ownership.h"
#else
import Creator.Core.Ownership;
#endif

#include "MaterialGraphPrimitiveCacheContract.h"
#include "MaterialGraphSceneGpuData.h"
#include "MaterialGraphWork.h"
#include "MaterialGraphSceneAccess.h"
#include "RHI/RHIShaderSource.h"

namespace material_graph
{
    struct ScenePrimitiveCacheResources final
    {
        struct ReconstructionPipeline
        {
            own::weak_owner<const Generation> m_generation;
            RHIPipelineHandle m_pipeline;
        };
        ce::fixed_array<LX::Runtime::ComputePipeline, 2> m_compute;
        ce::engine_ordered_map<std::uint32_t, ReconstructionPipeline> m_reconstructionPipelines;
        std::mutex m_declarationMutex;
        ce::dynamic_array<EnhancedRenderGraph::RGPassUsage> m_declarationUses;
        ce::dynamic_array<std::size_t> m_normalizationScratch;

        bool Prepare(const EnhancedFrameContext& context, std::string& error)
        {
            ce::erase_if(m_reconstructionPipelines,
                          [](const auto& item) { return item.second.m_generation.expired(); });
            if (m_compute[0].IsValid() && m_compute[1].IsValid())
            {
                return true;
            }
            const ce::fixed_array parameters{RHILayout::Cbv(0),
                                        RHILayout::SrvTable(1, 0),
                                        RHILayout::SrvBufferTable(3, 1),
                                        RHILayout::SrvBufferArray(SceneGpuData::kGeometryPageSize, 256),
                                        RHILayout::SrvBufferArray(SceneGpuData::kGeometryPageSize, 512),
                                        RHILayout::UavBufferTable(2, 0),
                                        RHILayout::Cbv(8)};
            const auto layout = context.rootSignatures->GetOrCreate({parameters, {}}, error);
            if (!layout.IsValid())
            {
                return false;
            }
            const ce::fixed_array entries{"ClearPrimitiveCache", "PopulatePrimitiveCache"};
            const auto source = RHIShaderSource::Resolve("MaterialGraphPrimitiveCache.slang").string();
            for (std::size_t stage = 0; stage < m_compute.size(); ++stage)
            {
                if (m_compute[stage].IsValid())
                {
                    continue;
                }
                LX::Runtime::CompiledCompute shader;
                RHIShaderCompileOptions options;
                options.strictMath = true;
                if (!LX::Runtime::CompileCompute(source, entries[stage], {}, options, shader, error, "cs_6_6"))
                {
                    return false;
                }
                RHIComputePipelineDesc description;
                description.layout = layout;
                description.csBytecode = shader.stage.bytecode.Data();
                description.csSize = shader.stage.bytecode.Size();
                if (!m_compute[stage].Create(*context.psoManager, description, std::move(shader.description), error))
                {
                    return false;
                }
            }
            return true;
        }

        bool PrepareReconstruction(const EnhancedFrameContext& context,
                                   const own::shared_owner<const Generation>& generation,
                                   const LX::Runtime::GraphicsPipeline& source, const RHIShaderBlob& meshShader,
                                   const RHIShaderBlob& pixelShader, bool temporal, RHIPipelineHandle& result,
                                   std::string& error)
        {
            const auto sourceId = source.GetHandle().id;
            if (const auto found = m_reconstructionPipelines.find(sourceId); found != m_reconstructionPipelines.end())
            {
                const auto owner = found->second.m_generation.lock();
                if (owner && generation && std::addressof(*owner) == std::addressof(*generation))
                {
                    result = found->second.m_pipeline;
                    return true;
                }
            }
            if (!source.IsValid() || !meshShader.Size() || !pixelShader.Size())
            {
                error = "METEOR primitive cache requires its separately cooked reconstruction stages.";
                return false;
            }
            const auto& raster = source.GetDesc();
            RHIMeshPipelineDesc description;
            description.msBytecode = meshShader.Data();
            description.msSize = meshShader.Size();
            description.psBytecode = pixelShader.Data();
            description.psSize = pixelShader.Size();
            description.layout = raster.layout;
            description.fillMode = raster.fillMode;
            description.cullMode = RHICullMode::None;
            description.depthEnable = temporal ? raster.depthEnable : false;
            description.blendEnable = raster.blendEnable;
            description.depthWriteMask = temporal ? raster.depthWriteMask : RHIDepthWrite::Zero;
            description.depthFunc = temporal ? RHICompareOp::Always : raster.depthFunc;
            description.independentBlend = raster.independentBlend;
            description.numRenderTargets = raster.numRenderTargets;
            description.dsvFormat = raster.dsvFormat;
            description.sampleCount = raster.sampleCount;
            for (std::size_t index = 0; index < std::size(description.rtvFormats); ++index)
            {
                description.renderTargetBlend[index] = raster.renderTargetBlend[index];
                description.rtvFormats[index] = raster.rtvFormats[index];
            }
            result = context.psoManager->GetOrCreateMesh(description, error);
            if (!result.IsValid())
            {
                return false;
            }
            // base PSO id 외에 봉인 generation도 대조한다. 토글 중인 이전 프레임은
            // Draw에 게시된 handle을 보유하므로 이 lookup의 교체를 관찰하지 않는다.
            m_reconstructionPipelines.insert_or_assign(sourceId, ReconstructionPipeline{generation, result});
            return true;
        }
    };

    class ScenePrimitiveCacheFrame final
    {
      public:
        ScenePrimitiveCacheFrame() = default;
        ScenePrimitiveCacheFrame(const ScenePrimitiveCacheFrame&) = delete;
        ScenePrimitiveCacheFrame& operator=(const ScenePrimitiveCacheFrame&) = delete;

        ~ScenePrimitiveCacheFrame()
        {
            if (m_device)
            {
                for (const auto buffer : m_buffers)
                {
                    ReleaseSceneBuffer(*m_device, m_pool, buffer);
                }
            }
        }

        static bool SupportsPacketDispatch(const SceneMaterialWork& work)
        {
            if (!work.packetWork || work.buckets.empty())
            {
                return false;
            }
            const auto& values = work.packetWork->values;
            // Mesh와 compute의 indirect XYZ ABI는 같지만 Vulkan의 장치별 한도는 다를 수 있다.
            // compute가 보장하는 축별 65535 범위 밖이면 기존 복원 경로를 유지한다.
            return values.dispatchWidth != 0 && values.dispatchWidth <= 65535 &&
                   std::uint64_t{values.sliceCapacity} <= std::uint64_t{values.dispatchWidth} * 65535;
        }

        bool Prepare(const EnhancedFrameContext& context, own::shared_owner<TransientBufferPool> pool,
                     own::shared_owner<ScenePrimitiveCacheResources> resources,
                     own::shared_owner<const SceneGpuData> sceneData, own::shared_owner<const SceneMaterialWork> material,
                     const math::matrix4x4& projection, std::string& error)
        {
            if (m_device)
            {
                error = "METEOR primitive cache frame preparation cannot replace retained storage.";
                return false;
            }
            m_device = context.resources;
            m_pool = std::move(pool);
            m_resources = std::move(resources);
            m_sceneData = std::move(sceneData);
            m_material = std::move(material);
            if (!m_device || !m_pool || !m_resources || !m_sceneData || !m_material ||
                !SupportsPacketDispatch(*m_material) || !context.width || !context.height ||
                m_sceneData->records.size() > UINT32_MAX)
            {
                error = "METEOR primitive cache requires sealed material packets, scene geometry and pooled storage.";
                return false;
            }
            m_recording = m_device->GetCurrentUploadRecordingId();
            m_descriptors = m_device->GetDescriptorVersionToken();
            if (!m_resources->Prepare(context, error))
            {
                return false;
            }
            for (std::size_t stage = 0; stage < m_compute.size(); ++stage)
            {
                m_compute[stage] = m_resources->m_compute[stage].GetGeneration();
            }
            struct Constants
            {
                math::matrix4x4 projection;
                std::uint32_t width, height, recordCount, capacity;
            };
            static_assert(sizeof(Constants) == 80);
            const Constants constants{math::transpose(projection), context.width, context.height,
                                      static_cast<std::uint32_t>(m_sceneData->records.size()),
                                      ScenePrimitiveCacheContract::kCapacity};
            m_constants = m_device->UploadConstants(&constants, sizeof(constants));
            if (!m_constants.IsValid())
            {
                error = "METEOR primitive cache constants upload failed.";
                return false;
            }
            const ce::fixed_array descriptions{RHIBufferDesc{.bytes = ScenePrimitiveCacheContract::kKeyBytes,
                                                        .allowUnorderedAccess = true,
                                                        .debugName = L"METEOR.Scene.PrimitiveCache.Keys"},
                                          RHIBufferDesc{.bytes = ScenePrimitiveCacheContract::kDataBytes,
                                                        .allowUnorderedAccess = true,
                                                        .debugName = L"METEOR.Scene.PrimitiveCache.Data"}};
            if (!m_pool->AcquireBatch(descriptions, m_buffers, error))
            {
                return false;
            }
            const ce::fixed_array inputDescriptions{
                RHIBindingDesc::SrvBuffer(m_sceneData->recordBuffer->Buffer(), constants.recordCount,
                                          sizeof(SceneGpuData::Record)),
                RHIBindingDesc::SrvBuffer(m_material->work, m_material->PacketCapacity(), 8),
                RHIBindingDesc::SrvBuffer(m_material->ranges, static_cast<std::uint32_t>(m_material->buckets.size()),
                                          16)};
            const ce::fixed_array outputDescriptions{
                RHIBindingDesc::UavBuffer(Keys(), ScenePrimitiveCacheContract::kCapacity, sizeof(std::uint32_t)),
                RHIBindingDesc::UavBuffer(Data(), ScenePrimitiveCacheContract::kCapacity,
                                          ScenePrimitiveCacheContract::kTriangleBytes)};
            m_inputs = m_device->CreateBindings(inputDescriptions);
            m_outputs = m_device->CreateBindings(outputDescriptions);
            if (!m_inputs.IsValid() || !m_outputs.IsValid() || m_device->GetCurrentUploadRecordingId() != m_recording ||
                m_device->GetDescriptorVersionToken() != m_descriptors)
            {
                error = "METEOR primitive cache bindings are incomplete or crossed their recording lifetime.";
                return false;
            }
            return true;
        }

        RHIBufferHandle Keys() const { return m_buffers[0]; }
        RHIBufferHandle Data() const { return m_buffers[1]; }

        void AddRead(const EnhancedRenderGraph& graph, auto& uses) const
        {
            CheckCurrent(&graph);
            if (!m_declared || !m_graphKeys.IsValid() || !m_graphData.IsValid())
            {
                throw std::runtime_error("METEOR primitive cache requires its explicit population pass.");
            }
            uses.push_back({m_graphKeys, RHIResourceState::PixelShaderResource, RGAccessMode::Read});
            uses.push_back({m_graphData, RHIResourceState::PixelShaderResource, RGAccessMode::Read});
        }

        template<class FrameOwner>
        void Declare(EnhancedRenderGraph& graph, RGHandle visibility, FrameOwner owner) const
        {
            CheckCurrent();
            // 선언은 다른 작업을 실행하거나 기다리지 않는다. 같은 Resources의
            // 별도 프레임/그래프는 공용 scratch가 반환될 때까지 기다릴 수 있다.
            const std::lock_guard declarationLock(m_resources->m_declarationMutex);
            owner->CheckCurrent(&graph);
            const auto materialInputs = m_material->GraphInputs(graph);
            if (m_graphOwner == graph.DeclarationGraph() && m_graphEpoch == graph.DeclarationEpoch())
            {
                if (!m_declared)
                {
                    throw std::runtime_error("METEOR primitive cache declaration did not complete.");
                }
                if (visibility.index != m_graphVisibility.index || visibility.version != m_graphVisibility.version ||
                    visibility.kind != m_graphVisibility.kind || visibility.epoch != m_graphVisibility.epoch)
                {
                    throw std::runtime_error("METEOR primitive cache received a different Visibility version.");
                }
                return;
            }
            if (m_graphOwner || !visibility.IsValid() ||
                graph.GetSchedulingMode() != RGSchedulingMode::ExplicitVersioned)
            {
                throw std::runtime_error("METEOR primitive cache requires one ExplicitVersioned recording.");
            }
            m_graphOwner = graph.DeclarationGraph();
            m_graphEpoch = graph.DeclarationEpoch();
            m_graphVisibility = visibility;
            m_graphKeys = graph.ImportBuffer(Keys(), RHIResourceState::Common, "METEOR.Scene.PrimitiveCache.Keys");
            m_graphData = graph.ImportBuffer(Data(), RHIResourceState::Common, "METEOR.Scene.PrimitiveCache.Data");
            graph.RequireImportedFinalState(m_graphKeys, RHIResourceState::Common);
            graph.RequireImportedFinalState(m_graphData, RHIResourceState::Common);
            m_graphKeys = graph.Write(m_graphKeys);
            const auto clear = graph.AddPass("METEOR.Scene.PrimitiveCache.Clear",
                                             {{m_graphKeys, RHIResourceState::UnorderedAccess, RGAccessMode::Write}},
                                             [this, owner](const auto& execution)
                                             {
                                                 owner->CheckCurrent(execution.graph);
                                                 CheckCurrent(execution.graph);
                                                 auto& encoder = *execution.encoder;
                                                 encoder.SetPipeline(RHIBindPoint::Compute, m_compute[0]->GetHandle());
                                                 encoder.SetConstantBuffer(RHIBindPoint::Compute, 0, m_constants);
                                                 encoder.SetBindings(RHIBindPoint::Compute, 5, m_outputs);
                                                 encoder.Dispatch(ScenePrimitiveCacheContract::kCapacity / 64, 1, 1);
                                             });
            graph.DeclareRecordingIndependent(clear);

            // Clear -> Populate의 UAV 경계와 Populate -> pixel SRV 경계가 key/data의
            // 게시를 보장한다. key를 먼저 CAS해도 어떤 소비자도 미완성 data를 읽지 않는다.
            m_graphKeys = graph.Modify(m_graphKeys);
            m_graphData = graph.Write(m_graphData);
            auto& uses = m_resources->m_declarationUses;
            uses.clear();
            uses.push_back({m_graphKeys, RHIResourceState::UnorderedAccess, RGAccessMode::ReadWrite});
            uses.push_back({m_graphData, RHIResourceState::UnorderedAccess, RGAccessMode::Write});
            uses.push_back({visibility, RHIResourceState::ShaderResource, RGAccessMode::Read});
            m_material->AddRead(graph, uses, materialInputs);
            m_sceneData->AddRead(graph, uses);
            NormalizeSceneReads(graph, uses, m_resources->m_normalizationScratch);
            const auto populate = graph.AddPass(
                "METEOR.Scene.PrimitiveCache.Populate", uses,
                [this, owner, visibility](const auto& execution)
                {
                    owner->CheckCurrent(execution.graph);
                    CheckCurrent(execution.graph);
                    const auto visibilityDescription =
                        RHIBindingDesc::Srv2D(execution.ResolveHandle(visibility), RHIFormat::RG32Uint);
                    const auto visibilityBinding = m_device->CreateBindings({&visibilityDescription, 1});
                    CheckCurrent(execution.graph);
                    if (!visibilityBinding.IsValid())
                    {
                        throw std::runtime_error("METEOR primitive cache Visibility binding failed.");
                    }
                    auto& encoder = *execution.encoder;
                    encoder.SetPipeline(RHIBindPoint::Compute, m_compute[1]->GetHandle());
                    encoder.SetConstantBuffer(RHIBindPoint::Compute, 0, m_constants);
                    encoder.SetBindings(RHIBindPoint::Compute, 1, visibilityBinding);
                    encoder.SetBindings(RHIBindPoint::Compute, 2, m_inputs);
                    encoder.SetBindings(RHIBindPoint::Compute, 5, m_outputs);
                    for (std::size_t bucketIndex = 0; bucketIndex < m_material->buckets.size(); ++bucketIndex)
                    {
                        const auto& bucket = m_material->buckets[bucketIndex];
                        const auto& draw = owner->draws.at(bucket.representative);
                        const auto& page = m_sceneData->pages.at(m_sceneData->recordPages.at(draw.geometryRecord));
                        encoder.SetBindings(RHIBindPoint::Compute, 3, page.vertexTable);
                        encoder.SetBindings(RHIBindPoint::Compute, 4, page.topologyTable);
                        const auto range = m_material->packetWork->plan->buckets.at(bucketIndex);
                        for (std::uint32_t index = range.first; index < range.first + range.count; ++index)
                        {
                            const auto& slice = m_material->packetWork->slices[index];
                            encoder.SetConstantBuffer(RHIBindPoint::Compute, 6, slice.selector);
                            if (!encoder.DispatchIndirect(m_material->arguments, slice.argumentOffset))
                            {
                                throw std::runtime_error("METEOR primitive cache packet dispatch failed.");
                            }
                        }
                    }
                });
            graph.DeclareRecordingIndependent(populate);
            m_declared = true;
        }

      private:
        void CheckCurrent(const EnhancedRenderGraph* graph = nullptr) const
        {
            if (!m_device || m_device->GetCurrentUploadRecordingId() != m_recording ||
                m_device->GetDescriptorVersionToken() != m_descriptors)
            {
                throw std::runtime_error("METEOR primitive cache has stale upload/descriptor ownership.");
            }
            if (graph && (graph->DeclarationGraph() != m_graphOwner || graph->DeclarationEpoch() != m_graphEpoch))
            {
                throw std::runtime_error("METEOR primitive cache belongs to a different or reset graph.");
            }
            if (graph && !m_declared)
            {
                throw std::runtime_error("METEOR primitive cache declaration did not complete.");
            }
        }

        IRenderDeviceServices* m_device{};
        own::shared_owner<TransientBufferPool> m_pool;
        own::shared_owner<ScenePrimitiveCacheResources> m_resources;
        own::shared_owner<const SceneGpuData> m_sceneData;
        own::shared_owner<const SceneMaterialWork> m_material;
        ce::fixed_array<own::shared_owner<const LX::Runtime::ComputeGeneration>, 2> m_compute;
        ce::fixed_array<RHIBufferHandle, 2> m_buffers{};
        RHIBufferSlice m_constants;
        RHIBindingTable m_inputs, m_outputs;
        std::uint64_t m_recording{}, m_descriptors{};
        mutable const EnhancedRenderGraph* m_graphOwner{};
        mutable std::uint64_t m_graphEpoch{};
        mutable bool m_declared{};
        mutable RGHandle m_graphKeys, m_graphData, m_graphVisibility;
    };
} // namespace material_graph
