#ifndef DYNAMICCPP_EXPORTS
#include "EnhancedSpritePass.h"
#include "EnhancedGBufferPass.h"
#include "../../Graph/EnhancedRenderGraph.h"
#include "../../../RHI/RHIEncoder.h"
#include "../../../RHI/DX12/DX12ShaderCompiler.h"
#include "../../../Texture.h"

#include <algorithm>
#include <cstring>
#include <vector>

namespace
{
    constexpr const char* kWorldSpriteShaderFile = "WorldSprite.hlsl";

    bool CompileWorldSpriteShader(const char* entry, const char* target,
        RHIShaderBlob& outBlob, std::string& outError)
    {
        return DX12ShaderCompiler::CompileFile(
            kWorldSpriteShaderFile, entry, target, outBlob, outError);
    }
}

bool EnhancedSpritePass::Initialize(const EnhancedFrameContext& context,
    std::string& outError)
{
    if (nullptr == context.resources || nullptr == context.psoManager ||
        nullptr == context.rootSignatures)
    {
        outError = "스프라이트 패스 컨텍스트가 불완전하다";
        return false;
    }
    return CreatePipelines(context, outError);
}

bool EnhancedSpritePass::CreatePipelines(const EnhancedFrameContext& context,
    std::string& outError)
{
    const RHIPipelineLayoutParam params[] = {
        RHILayout::Cbv(0),
        RHILayout::Srv(0),
        RHILayout::SrvTable(1, 1),
    };
    const RHIStaticSamplerDesc samplers[] = {
        { RHISampler::Linear(RHIAddressMode::Clamp), 0, RHIShaderVisibility::Pixel },
    };
    RHIPipelineLayoutDesc layoutDesc{};
    layoutDesc.params = params;
    layoutDesc.staticSamplers = samplers;
    const auto layout = context.rootSignatures->GetOrCreate(layoutDesc, outError);
    if (!layout.IsValid()) return false;

    RHIShaderBlob vs;
    RHIShaderBlob ps;
    if (!CompileWorldSpriteShader("VSMain", "vs_5_0", vs, outError) ||
        !CompileWorldSpriteShader("PSMain", "ps_5_0", ps, outError)) return false;

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
    if (!m_overlayPso.IsValid()) return false;

    desc.depthEnable = true;
    desc.depthWriteMask = RHIDepthWrite::Zero;
    desc.depthFunc = RHICompareOp::LessEqual;
    m_depthPso = context.psoManager->GetOrCreate(desc, outError);
    return m_depthPso.IsValid();
}

bool EnhancedSpritePass::PrepareFrame(const EnhancedFrameContext& context,
    std::string& outError)
{
    m_width = context.width;
    m_height = context.height;
    m_viewProjection = context.camera
        ? XMMatrixMultiply(context.camera->view, context.camera->projection)
        : XMMatrixIdentity();
    m_instances.clear();
    m_batches.clear();
    m_lastItemCount = 0;
    m_lastBatchCount = 0;
    if (nullptr == m_items || m_items->empty()) return true;

    std::vector<uint32_t> order(m_items->size());
    for (uint32_t i = 0; i < order.size(); ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [this](uint32_t a, uint32_t b)
    {
        const Item& lhs = (*m_items)[a];
        const Item& rhs = (*m_items)[b];
        if (lhs.canvasOrder != rhs.canvasOrder)
            return lhs.canvasOrder < rhs.canvasOrder;
        return lhs.layerOrder < rhs.layerOrder;
    });

    m_instances.reserve(order.size());
    for (uint32_t index : order)
    {
        const Item& item = (*m_items)[index];
        Instance instance{};
        instance.world = XMMatrixTranspose(item.world);
        instance.uv = item.uv;
        instance.color = item.color;
        m_instances.push_back(instance);

        if (!m_batches.empty() && m_batches.back().texture == item.texture &&
            m_batches.back().enableDepth == item.enableDepth)
        {
            ++m_batches.back().count;
        }
        else
        {
            Batch batch{};
            batch.first = static_cast<uint32_t>(m_instances.size() - 1);
            batch.count = 1;
            batch.texture = item.texture;
            batch.enableDepth = item.enableDepth;
            m_batches.push_back(batch);
        }
    }

    m_lastItemCount = static_cast<uint32_t>(m_instances.size());
    m_lastBatchCount = static_cast<uint32_t>(m_batches.size());
    if (nullptr != context.textureCache)
    {
        for (Batch& batch : m_batches)
        {
            std::string uploadError;
            batch.uploaded = context.textureCache->GetOrUpload(batch.texture, uploadError);
            if (!batch.uploaded.IsValid() && !uploadError.empty())
            {
                outError = "스프라이트 텍스처 업로드 실패: " + uploadError;
                return false;
            }
        }
    }
    return true;
}

void EnhancedSpritePass::Declare(EnhancedRenderGraph& graph,
    const EnhancedFrameContext& context)
{
    m_output = {};
    if (!m_overlayPso.IsValid() || !m_depthPso.IsValid() ||
        0 == m_width || 0 == m_height || m_instances.empty()) return;

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
    else m_output = m_inputs.color;

    std::vector<EnhancedRenderGraph::RGPassUsage> usages = {
        { m_output, RHIResourceState::RenderTarget },
    };
    if (m_inputs.depth.IsValid())
        usages.push_back({ m_inputs.depth, RHIResourceState::DepthRead });

    graph.AddPass(GetName(), usages,
        [this, &context, ownsColor](const EnhancedRenderGraph::ExecuteContext& ec)
        {
            const RHITextureHandle colors[] = { ec.ResolveHandle(m_output) };
            RHIRenderTargetBinding targets{};
            if (m_inputs.depth.IsValid())
            {
                const auto depth = RHIDepthTargetDesc::DepthReadOnly(
                    ec.ResolveHandle(m_inputs.depth), EnhancedGBufferPass::kDepthFormat);
                targets = context.resources->CreateRenderTargets(colors, &depth);
            }
            else targets = context.resources->CreateRenderTargets(colors);
            if (!targets.IsValid()) return;

            RHIEncoder& encoder = *ec.encoder;
            encoder.SetViewportAndScissor(m_width, m_height);
            encoder.BindRenderTargets(targets);
            if (ownsColor)
            {
                constexpr float clear[4] = { 0.f, 0.f, 0.f, 0.f };
                encoder.ClearRenderTargets(targets, clear);
            }
            const Mathf::Matrix viewProjection = XMMatrixTranspose(m_viewProjection);
            const auto constants = context.resources->UploadConstants(
                &viewProjection, sizeof(viewProjection));
            if (!constants.IsValid()) return;

            const uint64_t bytes = sizeof(Instance) *
                static_cast<uint64_t>(m_instances.size());
            const auto upload = context.resources->AllocateUpload(
                RHIUploadRequest{ bytes, RHIUploadUsage::BufferCopy, sizeof(Instance) });
            if (!upload.IsValid()) return;
            memcpy(upload.cpuAddress, m_instances.data(), static_cast<size_t>(bytes));

            encoder.SetPrimitiveTopology(RHIPrimitiveTopology::TriangleStrip);
            for (const Batch& batch : m_batches)
            {
                if (!batch.uploaded.IsValid()) continue;
                const RHIBindingDesc texture[] = {
                    RHIBindingDesc::Srv2D(batch.uploaded.handle,
                        batch.uploaded.format, 0, batch.uploaded.mipLevels),
                };
                const auto table = context.resources->CreateBindings(texture);
                if (!table.IsValid()) break;

                const RHIPipelineHandle pso = batch.enableDepth &&
                    m_inputs.depth.IsValid() ? m_depthPso : m_overlayPso;
                encoder.SetPipeline(RHIBindPoint::Graphics, pso);
                encoder.SetConstantBuffer(RHIBindPoint::Graphics, 0, constants);
                encoder.SetRootBuffer(RHIBindPoint::Graphics, 1,
                    upload.SubRange(sizeof(Instance) * static_cast<uint64_t>(batch.first),
                        sizeof(Instance) * static_cast<uint64_t>(batch.count)));
                encoder.SetBindings(RHIBindPoint::Graphics, 2, table);
                encoder.Draw(4, batch.count);
            }
        });
}

void EnhancedSpritePass::Shutdown()
{
    m_instances.clear();
    m_batches.clear();
    m_width = 0;
    m_height = 0;
    m_lastItemCount = 0;
    m_lastBatchCount = 0;
    m_depthPso = {};
    m_overlayPso = {};
}
#endif
