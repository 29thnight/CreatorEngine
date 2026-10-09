#include "EnhancedSpritePass.h"
#include "../../../RHI/RHIEncoder.h"
#include "../../../RHI/RHIShaderCompiler.h"
#include "../../../Texture.h"
#include "../../Graph/EnhancedRenderGraph.h"
#include "../../Graph/ShadowCasterBounds.h"
#include "EnhancedGBufferPass.h"

#include <algorithm>
#include <bit>
#include <cstring>
#include <stdexcept>
#include <utility>
#include <vector>

namespace
{
    constexpr const char *kWorldSpriteShaderFile = "WorldSprite.slang";
    struct WorldSpriteConstants
    {
        math::matrix4x4 viewProjection{};
        math::matrix4x4 currentUnjittered{};
        math::matrix4x4 previousUnjittered{};
        float screenDimensions[2]{};
        uint32_t reset{};
        uint32_t instanceBase{};
    };
    static_assert(sizeof(WorldSpriteConstants) == 208);
    static_assert(offsetof(WorldSpriteConstants, instanceBase) == 204);

    bool CompileWorldSpriteShader(const char *entry, const char *target, RHIShaderBlob &outBlob, std::string &outError)
    {
        return RHIShaderCompiler::CompileFile(kWorldSpriteShaderFile, entry, target, outBlob, outError);
    }
} // namespace

bool EnhancedSpritePass::Initialize(const EnhancedFrameContext &context, std::string &outError)
{
    if (nullptr == context.resources || nullptr == context.psoManager || nullptr == context.rootSignatures)
    {
        outError = "스프라이트 패스 컨텍스트가 불완전하다";
        return false;
    }
    return CreatePipelines(context, outError);
}

bool EnhancedSpritePass::CreatePipelines(const EnhancedFrameContext &context, std::string &outError)
{
    const RHIPipelineLayoutParam params[] = {
        RHILayout::Cbv(0),
        RHILayout::Srv(0),
        RHILayout::SrvTable(1, 1),
    };
    const RHIStaticSamplerDesc samplers[] = {
        {RHISampler::Linear(RHIAddressMode::Clamp), 0, RHIShaderVisibility::Pixel},
    };
    RHIPipelineLayoutDesc layoutDesc{};
    layoutDesc.params = params;
    layoutDesc.staticSamplers = samplers;
    const auto layout = context.rootSignatures->GetOrCreate(layoutDesc, outError);
    if (!layout.IsValid())
    {
        return false;
    }

    RHIShaderBlob vs;
    RHIShaderBlob ps;
    if (!CompileWorldSpriteShader("VSMain", "vs_5_0", vs, outError) ||
        !CompileWorldSpriteShader("PSMain", "ps_5_0", ps, outError))
    {
        return false;
    }

    RHIGraphicsPipelineDesc desc{};
    desc.vsBytecode = vs.Data();
    desc.vsSize = vs.Size();
    desc.psBytecode = ps.Data();
    desc.psSize = ps.Size();
    desc.layout = layout;
    desc.topologyType = RHITopologyType::Triangle;
    desc.blendEnable = true;
    desc.cullMode = RHICullMode::None;
    desc.numRenderTargets = 1;
    desc.rtvFormats[0] = m_outputFormat;
    // 두 PSO 모두 같은 render target binding을 사용한다. Overlay 배치는 depth
    // test만 끌 뿐 D32 attachment 자체는 붙어 있으므로 Vulkan dynamic rendering
    // 호환성을 위해 attachment format은 양쪽 파이프라인에 선언해야 한다.
    desc.dsvFormat = EnhancedGBufferPass::kDepthFormat;

    desc.depthEnable = false;
    m_overlayPso = context.psoManager->GetOrCreate(desc, outError);
    if (!m_overlayPso.IsValid())
    {
        return false;
    }

    desc.depthEnable = true;
    desc.depthWriteMask = RHIDepthWrite::Zero;
    desc.depthFunc = RHICompareOp::LessEqual;
    m_depthPso = context.psoManager->GetOrCreate(desc, outError);
    if (!m_depthPso.IsValid()) return false;

    const RHIPipelineLayoutParam temporalParams[] = {
        RHILayout::Cbv(0), RHILayout::Srv(0), RHILayout::SrvTable(1, 1), RHILayout::SrvTable(1, 2),
    };
    layoutDesc.params = temporalParams;
    const auto temporalLayout = context.rootSignatures->GetOrCreate(layoutDesc, outError);
    if (!temporalLayout.IsValid()) return false;
    RHIShaderBlob temporalVs, temporalPs;
    if (!CompileWorldSpriteShader("TemporalVS", "vs_5_0", temporalVs, outError) ||
        !CompileWorldSpriteShader("TemporalPS", "ps_5_0", temporalPs, outError)) return false;
    desc.layout = temporalLayout;
    desc.vsBytecode = temporalVs.Data(); desc.vsSize = temporalVs.Size();
    desc.psBytecode = temporalPs.Data(); desc.psSize = temporalPs.Size();
    desc.numRenderTargets = 4;
    desc.rtvFormats[0] = RHIFormat::RG16Float;
    desc.rtvFormats[1] = desc.rtvFormats[2] = desc.rtvFormats[3] = RHIFormat::R16Float;
    // Color sprites depth-test against the original opaque scene and never
    // against one another. TemporalPS performs that identical source test;
    // the dedicated temporal depth follows the last visible painted sprite.
    desc.depthFunc = RHICompareOp::Always;
    desc.depthWriteMask = RHIDepthWrite::All;
    desc.blendEnable = false;
    desc.independentBlend = true;
    desc.renderTargetBlend[0].writeMask = 0x3;
    for (unsigned i = 1; i < 4; ++i)
    {
        auto& blend = desc.renderTargetBlend[i];
        blend.enable = true;
        blend.srcColor = RHIBlendFactor::One;
        blend.dstColor = RHIBlendFactor::InvSrcAlpha;
        blend.writeMask = 0x1;
    }
    m_temporalPso = context.psoManager->GetOrCreate(desc, outError);
    return m_temporalPso.IsValid();
}

bool EnhancedSpritePass::PrepareFrame(const EnhancedFrameContext &context, std::string &outError)
{
    m_width = context.width;
    m_height = context.height;
    m_viewProjection = context.camera ? context.camera->view * context.camera->projection : math::matrix4x4::identity();
    m_instances.clear();
    m_batches.clear();
    m_visibilityFrame.reset();
    m_visibilitySpheres.clear();
    m_gpuVisibilityEnabled = context.resources && context.resources->GetIndirectDrawCapabilities().nonIndexedDraw;
    m_lastItemCount = 0;
    m_lastBatchCount = 0;
    m_lastTemporalRecordedItemCount = 0;
    if (nullptr == m_items || m_items->empty())
    {
        return true;
    }

    if (m_items->size() > (std::numeric_limits<uint32_t>::max)())
    {
        outError = "Sprite instance count exceeds 32-bit addressing.";
        return false;
    }
    m_visibilitySpheres.reserve(m_items->size());
    std::vector<uint32_t> order(m_items->size());
    for (uint32_t i = 0; i < order.size(); ++i)
    {
        order[i] = i;
    }
    std::stable_sort(order.begin(), order.end(), [this](uint32_t a, uint32_t b) {
        const Item &lhs = (*m_items)[a];
        const Item &rhs = (*m_items)[b];
        if (lhs.canvasOrder != rhs.canvasOrder)
        {
            return lhs.canvasOrder < rhs.canvasOrder;
        }
        return lhs.layerOrder < rhs.layerOrder;
    });

    m_instances.reserve(order.size());
    for (uint32_t index : order)
    {
        const Item &item = (*m_items)[index];
        Instance instance{};
        instance.world = math::transpose(item.world);
        instance.previousWorld = math::transpose(item.previousWorldValid ? item.previousWorld : item.world);
        instance.uv = item.uv;
        instance.color = item.color;
        instance.sampling.x = item.signedDistance ? 1.f : 0.f;
        instance.sampling.y = item.previousWorldValid ? 1.f : 0.f;
        instance.sampling.z = item.previousAppearanceValid ? 1.f : 0.f;
        instance.sampling.w = item.enableDepth ? 1.f : 0.f;
        m_instances.push_back(instance);
        EnhancedDrawItem bounds{};
        bounds.worldMatrix = item.world;
        // Encloses the procedural [-.5,.5] XY quad, rounded upward.
        bounds.boundRadius = 0.707107f;
        const auto sphere = shadow_math::WorldBounds(bounds);
        m_visibilitySpheres.push_back({sphere.center.x, sphere.center.y, sphere.center.z, sphere.radius});

        if (!m_batches.empty() && m_batches.back().textureId == TextureFramePins::Identity(item.texture) &&
            m_batches.back().enableDepth == item.enableDepth)
        {
            ++m_batches.back().count;
        }
        else
        {
            Batch batch{};
            batch.first = static_cast<uint32_t>(m_instances.size() - 1);
            batch.count = 1;
            batch.texturePinIndex = item.texturePinIndex;
            batch.texture = item.texture;
            batch.textureId = TextureFramePins::Identity(item.texture);
            batch.enableDepth = item.enableDepth;
            m_batches.push_back(std::move(batch));
        }
    }

    m_lastItemCount = static_cast<uint32_t>(m_instances.size());
    m_lastBatchCount = static_cast<uint32_t>(m_batches.size());
    if (nullptr != context.textureCache)
    {
        for (Batch &batch : m_batches)
        {
            std::string uploadError;
            batch.uploaded = context.textureCache->GetOrUpload(batch.texture, context.TextureImage(batch.texture), uploadError);
            if (!batch.uploaded.IsValid() || !uploadError.empty())
            {
                outError = "스프라이트 텍스처 업로드 실패: " + uploadError;
                return false;
            }
        }
    }
    return true;
}

bool EnhancedSpritePass::PrepareGpuVisibility(const EnhancedFrameContext& context, std::string& outError)
{
    m_visibilityFrame.reset();
    if (!m_gpuVisibilityEnabled || m_batches.empty())
    {
        outError.clear();
        return true;
    }
    if (!context.resources || !context.resources->GetIndirectDrawCapabilities().nonIndexedDraw ||
        m_visibilitySpheres.size() != m_instances.size())
    {
        outError = "Sprite GPU visibility lost its prepared geometry contract.";
        return false;
    }
    if (!m_visibility.PreparePipelines(context, outError))
    {
        return false;
    }
    std::vector<GpuGeometryVisibility::Candidate> candidates;
    std::vector<GpuGeometryVisibility::Bin> bins;
    candidates.reserve(m_instances.size());
    bins.reserve(m_batches.size());
    uint64_t outputOffset = 0;
    for (const auto& batch : m_batches)
    {
        const uint64_t count = batch.count;
        if (!count || uint64_t(batch.first) + count > m_instances.size() ||
            outputOffset + count > (std::numeric_limits<uint32_t>::max)())
        {
            outError = "Sprite GPU visibility exceeds valid instance/output ranges.";
            return false;
        }
        const uint32_t bin = static_cast<uint32_t>(bins.size());
        // Deliberate 16B nonindexed prefix of a 20B GPU-produced indexed record.
        // firstIndex and baseVertex MUST both remain zero (RHI ABI assertions).
        // Preserve the entire contiguous batch if any instance is visible; GPU
        // compaction may not reorder blending or change original instance IDs.
        bins.push_back({4u, 0u, 0, batch.count});
        for (uint32_t local = 0; local < batch.count; ++local)
        {
            const auto sphere = m_visibilitySpheres[batch.first + local];
            const uint32_t flags = sphere.w > 0.f ? 0u : GpuGeometryVisibility::kConservative;
            candidates.push_back({sphere, bin, local, static_cast<uint32_t>(outputOffset), flags});
        }
        outputOffset += count;
        outputOffset = (outputOffset + GpuGeometryVisibility::kOutputAlignment - 1u) &
            ~(uint64_t(GpuGeometryVisibility::kOutputAlignment) - 1u);
    }
    if (!m_visibility.Prepare(context, m_viewProjection, candidates, bins, m_visibilityFrame, outError))
    {
        return false;
    }
    if (!m_visibilityFrame)
    {
        outError = "Sprite GPU visibility did not produce a supported indirect frame.";
        return false;
    }
    return true;
}

void EnhancedSpritePass::Declare(EnhancedRenderGraph &graph, const EnhancedFrameContext &context)
{
    m_output = m_inputs.color;
    if (!m_overlayPso.IsValid() || !m_depthPso.IsValid() || 0 == m_width || 0 == m_height || m_instances.empty())
    {
        return;
    }

    const auto visibility = m_visibilityFrame;
    if (m_gpuVisibilityEnabled && !visibility)
    {
        throw std::runtime_error("Sprite GPU visibility must be prepared before graph declaration.");
    }
    if (visibility)
    {
        visibility->Declare(graph);
    }
    const bool ownsColor = !m_inputs.color.IsValid();
    if (ownsColor)
    {
        RGTextureDesc desc{};
        desc.width = m_width;
        desc.height = m_height;
        desc.format = m_outputFormat;
        desc.allowRenderTarget = true;
        desc.name = "Sprite.Output";
        m_output = graph.CreateTexture(desc);
    }
    else
    {
        m_output = m_inputs.color;
    }
    const bool explicitAccess = graph.GetSchedulingMode() != RGSchedulingMode::DeclarationOrder;
    if (graph.GetSchedulingMode() == RGSchedulingMode::ExplicitVersioned)
    {
        m_output = ownsColor ? graph.Write(m_output) : graph.Modify(m_output);
    }
    const auto colorAccess =
        explicitAccess ? (ownsColor ? RGAccessMode::Write : RGAccessMode::ReadWrite) : RGAccessMode::LegacyState;
    const auto readAccess = explicitAccess ? RGAccessMode::Read : RGAccessMode::LegacyState;
    const auto output = m_output;
    const auto depth = m_inputs.depth;

    std::vector<EnhancedRenderGraph::RGPassUsage> usages = {
        {output, RHIResourceState::RenderTarget, colorAccess},
    };
    if (m_inputs.depth.IsValid())
    {
        usages.push_back({depth, RHIResourceState::DepthRead, readAccess});
    }
    if (explicitAccess)
    {
        for (const auto &batch : m_batches)
        {
            if (!batch.uploaded.IsValid())
            {
                continue;
            }
            auto texture = graph.FindImportedTexture(batch.uploaded.handle);
            if (!texture.IsValid())
            {
                texture =
                    graph.ImportTexture(batch.uploaded.handle, RHIResourceState::PixelShaderResource, "Sprite.Texture");
            }
            if (std::none_of(usages.begin(), usages.end(), [texture](const auto &usage) {
                    return usage.handle.index == texture.index && usage.handle.version == texture.version;
                }))
            {
                usages.push_back({texture, RHIResourceState::PixelShaderResource, readAccess});
            }
        }
    }

    if (visibility)
    {
        visibility->AddReadUsages(graph, usages);
    }
    graph.AddPass(
        GetName(), usages, [this, &context, ownsColor, output, depth, visibility](const EnhancedRenderGraph::ExecuteContext &ec) {
            const RHITextureHandle colors[] = {ec.ResolveHandle(output)};
            RHIRenderTargetBinding targets{};
            if (depth.IsValid())
            {
                const auto depthTarget =
                    RHIDepthTargetDesc::DepthReadOnly(ec.ResolveHandle(depth), EnhancedGBufferPass::kDepthFormat);
                targets = context.resources->CreateRenderTargets(colors, &depthTarget);
            }
            else
            {
                targets = context.resources->CreateRenderTargets(colors);
            }
            if (!targets.IsValid())
            {
                throw std::runtime_error("Sprite render-target binding failed.");
            }

            RHIEncoder &encoder = *ec.encoder;
            encoder.SetViewportAndScissor(m_width, m_height);
            encoder.BindRenderTargets(targets);
            if (ownsColor)
            {
                constexpr float clear[4] = {0.f, 0.f, 0.f, 0.f};
                encoder.ClearRenderTargets(targets, clear);
            }
            WorldSpriteConstants camera{};
            camera.viewProjection = math::transpose(m_viewProjection);

            const uint64_t bytes = sizeof(Instance) * static_cast<uint64_t>(m_instances.size());
            const auto upload = context.resources->AllocateUpload(
                RHIUploadRequest{bytes, RHIUploadUsage::BufferCopy, 256});
            if (!upload.IsValid())
            {
                throw std::runtime_error("Sprite instance upload failed.");
            }
            memcpy(upload.cpuAddress, m_instances.data(), static_cast<size_t>(bytes));

            encoder.SetPrimitiveTopology(RHIPrimitiveTopology::TriangleStrip);
            for (uint32_t batchIndex = 0; batchIndex < m_batches.size(); ++batchIndex)
            {
                const Batch& batch = m_batches[batchIndex];
                if (!batch.uploaded.IsValid())
                {
                    throw std::runtime_error("Sprite texture upload is unavailable.");
                }
                const RHIBindingDesc texture[] = {
                    RHIBindingDesc::Srv2D(batch.uploaded.handle, batch.uploaded.format, 0, batch.uploaded.mipLevels),
                };
                const auto table = context.resources->CreateBindings(texture);
                if (!table.IsValid())
                {
                    throw std::runtime_error("Sprite texture bindings failed.");
                }

                const RHIPipelineHandle pso = batch.enableDepth && depth.IsValid() ? m_depthPso : m_overlayPso;
                encoder.SetPipeline(RHIBindPoint::Graphics, pso);
                camera.instanceBase = batch.first;
                const auto constants = context.resources->UploadConstants(&camera, sizeof(camera));
                if (!constants.IsValid())
                {
                    throw std::runtime_error("Sprite batch constant upload failed.");
                }
                encoder.SetConstantBuffer(RHIBindPoint::Graphics, 0, constants);
                // Full aligned storage binding; batch bases may not satisfy Vulkan
                // minStorageBufferOffsetAlignment and are applied in the shader.
                encoder.SetRootBuffer(RHIBindPoint::Graphics, 1, upload);
                encoder.SetBindings(RHIBindPoint::Graphics, 2, table);
                if (visibility)
                {
                    if (!encoder.DrawIndirect(visibility->Arguments(), visibility->ArgsOffset(batchIndex)))
                    {
                        throw std::runtime_error("Sprite nonindexed indirect submission failed.");
                    }
                }
                else
                {
                    encoder.Draw(4, batch.count);
                }
            }
        });
}

std::array<RGHandle, 5> EnhancedSpritePass::DeclareTemporal(EnhancedRenderGraph& graph,
    const EnhancedFrameContext& context, std::array<RGHandle, 5> outputs)
{
    if (m_instances.empty()) return outputs;
    if (!m_temporalPso.IsValid() || !m_width || !m_height || !m_output.IsValid() || !m_inputs.depth.IsValid() ||
        std::any_of(outputs.begin(), outputs.end(), [](RGHandle handle) { return !handle.IsValid(); }))
        throw std::runtime_error("Temporal sprite inputs or pipeline unavailable.");
    const auto visibility = m_visibilityFrame;
    if (m_gpuVisibilityEnabled && !visibility)
        throw std::runtime_error("Temporal sprites require the prepared color visibility frame.");

    const bool versioned = graph.GetSchedulingMode() == RGSchedulingMode::ExplicitVersioned;
    const bool explicitAccess = graph.GetSchedulingMode() != RGSchedulingMode::DeclarationOrder;
    const auto read = explicitAccess ? RGAccessMode::Read : RGAccessMode::LegacyState;
    const auto modify = explicitAccess ? RGAccessMode::ReadWrite : RGAccessMode::LegacyState;
    std::vector<EnhancedRenderGraph::RGPassUsage> uses;
    for (unsigned i = 0; i < outputs.size(); ++i)
    {
        if (versioned) outputs[i] = graph.Modify(outputs[i]);
        uses.push_back({outputs[i], i == 4 ? RHIResourceState::DepthWrite : RHIResourceState::RenderTarget, modify});
    }
    const auto sourceDepth = m_inputs.depth;
    uses.push_back({sourceDepth, RHIResourceState::PixelShaderResource, read});
    // Retain the exact Sprite color producer and its ordering even with TU off.
    // Reading this version is a sidecar dependency, not a shader color sample.
    uses.push_back({m_output, RHIResourceState::PixelShaderResource, read});
    for (const auto& batch : m_batches)
    {
        if (!batch.uploaded.IsValid())
            throw std::runtime_error("Temporal sprite texture was not uploaded for the color route.");
        if (!explicitAccess) continue;
        auto texture = graph.FindImportedTexture(batch.uploaded.handle);
        if (!texture.IsValid())
            texture = graph.ImportTexture(batch.uploaded.handle, RHIResourceState::PixelShaderResource, "Temporal.SpriteImage");
        if (std::none_of(uses.begin(), uses.end(), [texture](const auto& use) {
            return use.handle.index == texture.index && use.handle.version == texture.version &&
                use.handle.kind == texture.kind && use.handle.epoch == texture.epoch;
        })) uses.push_back({texture, RHIResourceState::PixelShaderResource, read});
    }
    if (visibility) visibility->AddReadUsages(graph, uses);

    WorldSpriteConstants camera{};
    camera.viewProjection = math::transpose(m_viewProjection);
    const auto& temporal = context.temporalFrame;
    const auto current = temporal.camera.valid ?
        std::bit_cast<math::matrix4x4>(temporal.camera.viewMatrix) *
            std::bit_cast<math::matrix4x4>(temporal.camera.projectionMatrix) : m_viewProjection;
    camera.currentUnjittered = math::transpose(current);
    camera.previousUnjittered = math::transpose(temporal.camera.valid ?
        current * std::bit_cast<math::matrix4x4>(temporal.camera.clipToPreviousClip) : current);
    camera.screenDimensions[0] = static_cast<float>(m_width);
    camera.screenDimensions[1] = static_cast<float>(m_height);
    camera.reset = temporal.reset || !temporal.camera.valid;

    graph.AddPass("Temporal.SpriteMotion", uses,
        [this, &context, outputs, sourceDepth, visibility, camera](const auto& execute) mutable {
            m_lastTemporalRecordedItemCount = 0;
            uint32_t recordedItemCount = 0;
            std::array<RHITextureHandle, 4> colors;
            for (unsigned i = 0; i < colors.size(); ++i) colors[i] = execute.ResolveHandle(outputs[i]);
            const auto depth = RHIDepthTargetDesc::Depth(
                execute.ResolveHandle(outputs[4]), EnhancedGBufferPass::kDepthFormat);
            const auto targets = context.resources->CreateRenderTargets(colors, &depth);
            if (!targets.IsValid()) throw std::runtime_error("Temporal sprite render targets unavailable.");
            const uint64_t bytes = sizeof(Instance) * static_cast<uint64_t>(m_instances.size());
            const auto upload = context.resources->AllocateUpload({bytes, RHIUploadUsage::BufferCopy, 256});
            const RHIBindingDesc sceneDepth[]{RHIBindingDesc::SrvDepth(execute.ResolveHandle(sourceDepth))};
            const auto depthTable = context.resources->CreateBindings(sceneDepth);
            if (!upload.IsValid() || !depthTable.IsValid())
                throw std::runtime_error("Temporal sprite instance/depth bindings failed.");
            memcpy(upload.cpuAddress, m_instances.data(), static_cast<size_t>(bytes));

            auto& encoder = *execute.encoder;
            encoder.BindRenderTargets(targets);
            encoder.SetViewportAndScissor(m_width, m_height);
            encoder.SetPipeline(RHIBindPoint::Graphics, m_temporalPso);
            encoder.SetPrimitiveTopology(RHIPrimitiveTopology::TriangleStrip);
            encoder.SetRootBuffer(RHIBindPoint::Graphics, 1, upload);
            encoder.SetBindings(RHIBindPoint::Graphics, 3, depthTable);
            for (uint32_t batchIndex = 0; batchIndex < m_batches.size(); ++batchIndex)
            {
                const auto& batch = m_batches[batchIndex];
                const RHIBindingDesc image[]{RHIBindingDesc::Srv2D(
                    batch.uploaded.handle, batch.uploaded.format, 0, batch.uploaded.mipLevels)};
                const auto table = context.resources->CreateBindings(image);
                camera.instanceBase = batch.first;
                const auto constants = context.resources->UploadConstants(&camera, sizeof(camera));
                if (!table.IsValid() || !constants.IsValid())
                    throw std::runtime_error("Temporal sprite image/constant bindings failed.");
                encoder.SetConstantBuffer(RHIBindPoint::Graphics, 0, constants);
                encoder.SetBindings(RHIBindPoint::Graphics, 2, table);
                if (visibility)
                {
                    if (!encoder.DrawIndirect(visibility->Arguments(), visibility->ArgsOffset(batchIndex)))
                        throw std::runtime_error("Temporal sprite indirect recording failed.");
                }
                else encoder.Draw(4, batch.count);
                // This is recording evidence only. An indirect visibility draw
                // can produce zero pixels; no GPU completion is inferred here.
                recordedItemCount += batch.count;
            }
            m_lastTemporalRecordedItemCount = recordedItemCount;
        }, true);
    return outputs;
}

void EnhancedSpritePass::Shutdown()
{
    m_visibilityFrame.reset();
    m_visibility.ShutdownAfterIdle();
    m_gpuVisibilityEnabled = false;
    m_visibilitySpheres.clear();
    m_instances.clear();
    m_batches.clear();
    m_texturePins.reset();
    m_width = 0;
    m_height = 0;
    m_lastItemCount = 0;
    m_lastBatchCount = 0;
    m_lastTemporalRecordedItemCount = 0;
    m_depthPso = {};
    m_overlayPso = {};
    m_temporalPso = {};
}
