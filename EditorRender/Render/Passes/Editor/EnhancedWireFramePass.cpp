#include "EnhancedWireFramePass.h"
#include "Render/Graph/EnhancedRenderGraph.h"
#include "RHI/RHIEncoder.h"
#include "Mesh.h"

#include <cstring>
#include <string>
#include "RHI/RHIShaderCompiler.h"

namespace
{
    // DX11 WireFrame.vs/ps의 이식. 원본 VS는 카메라 방향과 법선까지
    // 계산하지만 PS가 실제로 쓰는 것은 상수 색 하나다 — 그 둘은 뺀다.
    // 색 (0,1,0,1)은 원본 그대로.
    //
    // 스키닝은 원본에 있는 것이라 그대로 옮긴다. 판정 조건(boneWeight[0] > 0)과
    // 네 본의 가중 합이 같고, 규약만 다르다: DX11은 mul(bone, v)(열 벡터),
    // 여기는 mul(v, bone)(행 벡터)이다. CPU가 전치해서 올리므로 결과는 같다.
    //
    // 원본과 갈리는 점 하나: 원본은 위치에만 본을 먹인다(법선은 안 쓴다).
    // 여기도 위치만 변형한다 — 와이어프레임은 법선을 보지 않는다.
    constexpr const char* kWireFrameShaderFile = "WireFrame.hlsl";

    struct WireFrameConstants
    {
        Mathf::Matrix viewProjection{};   // 전치해서 넣는다
    };

    bool CompileWireFrameShader(const char* entry, const char* target,
        RHIShaderBlob& outBlob, std::string& outError)
    {
        return RHIShaderCompiler::CompileFile(kWireFrameShaderFile, entry, target, outBlob, outError);
    }
}

bool EnhancedWireFramePass::Initialize(const EnhancedFrameContext& context,
    std::string& outError)
{
    if (nullptr == context.resources || nullptr == context.psoManager ||
        nullptr == context.rootSignatures || nullptr == context.meshCache)
    {
        outError = "와이어프레임 패스 컨텍스트가 불완전하다";
        return false;
    }

    return CreatePipelines(context, outError);
}

bool EnhancedWireFramePass::CreatePipelines(const EnhancedFrameContext& context,
    std::string& outError)
{
    // b0 상수(뷰투영) · t0 인스턴스 · t1 본 팔레트(둘 다 루트 SRV).
    // 텍스처가 없어 디스크립터 테이블도 없다.
    const RHIPipelineLayoutParam params[] = {
        RHILayout::Cbv(0),
        RHILayout::Srv(0),
        RHILayout::Srv(1),
    };

    RHIPipelineLayoutDesc rootDesc{};
    rootDesc.params = params;
    rootDesc.allowInputAssembler = true;

    const auto root = context.rootSignatures->GetOrCreate(rootDesc, outError);
    if (!root.IsValid()) return false;

    RHIShaderBlob vsBlob;
    RHIShaderBlob psBlob;
    if (!CompileWireFrameShader("VSMain", "vs_5_0", vsBlob, outError)) return false;
    if (!CompileWireFrameShader("PSMain", "ps_5_0", psBlob, outError)) return false;

    // 엔진 Vertex에서 위치와 본 가중만 읽는다. 스트라이드는 정점 버퍼 뷰가
    // 정하므로 요소를 다 선언할 필요가 없다(GBuffer도 부분 선언이다).
    static const RHIInputElement kInputElements[] = {
        { "POSITION", 0, RHIFormat::RGB32Float, 0, 0, 0 },
        { "BLENDINDICES", 0, RHIFormat::RGBA32Float, 0, 64, 0 },
        { "BLENDWEIGHT", 0, RHIFormat::RGBA32Float, 0, 80, 0 },
    };
    static_assert(offsetof(Vertex, position) == 0, "Vertex 레이아웃이 바뀌었다");
    static_assert(offsetof(Vertex, boneIndices) == 64, "Vertex 레이아웃이 바뀌었다");
    static_assert(offsetof(Vertex, boneWeights) == 80, "Vertex 레이아웃이 바뀌었다");

    RHIGraphicsPipelineDesc desc{};
    desc.vsBytecode = vsBlob.Data();
    desc.vsSize = vsBlob.Size();
    desc.psBytecode = psBlob.Data();
    desc.psSize = psBlob.Size();
    desc.layout = root;
    desc.inputElements = kInputElements;
    desc.inputElementCount = _countof(kInputElements);
    desc.topologyType = RHITopologyType::Triangle;

    // ★ 이 패스의 요점 — 채우기 대신 선을 긋는다.
    desc.fillMode = RHIFillMode::Wireframe;

    // 원본과 같이 깊이는 보고(가려짐), 블렌딩은 없다(불투명 선).
    desc.depthEnable = true;
    desc.blendEnable = false;
    desc.cullMode = RHICullMode::None;
    desc.numRenderTargets = 1;
    desc.rtvFormats[0] = m_outputFormat;
    desc.dsvFormat = kDepthFormat;

    m_pso = context.psoManager->GetOrCreate(desc, outError);
    if (!m_pso.IsValid()) return false;

    return true;
}

void EnhancedWireFramePass::CollectDraws(const std::vector<EnhancedDrawItem>* draws)
{
    if (nullptr == draws) return;

    for (const EnhancedDrawItem& draw : *draws)
    {
        if (nullptr == draw.mesh) continue;

        ++m_lastDrawItemCount;

        // 배치는 (메시)로만 가른다 — 재질은 이 패스에 없다.
        Batch* batch = nullptr;
        for (Batch& existing : m_batches)
        {
            if (existing.mesh == draw.mesh) { batch = &existing; break; }
        }

        if (nullptr == batch)
        {
            m_batches.push_back(Batch{ draw.mesh, 0, 0 });
            batch = &m_batches.back();
        }
        ++batch->count;

        // 본 팔레트를 애니메이터별로 한 번씩만 담는다(GBuffer와 같은 규약).
        //
        // 한 캐릭터의 메시가 여럿이면 드로우도 여럿인데 팔레트는 하나다 —
        // 중복 제거가 없으면 같은 512행렬을 메시 수만큼 올린다.
        if (nullptr != draw.bonePalette && 0 != draw.boneCount)
        {
            if (m_boneOffsets.find(draw.animatorKey) == m_boneOffsets.end())
            {
                const uint32_t offset = static_cast<uint32_t>(m_bonePalettes.size());
                m_bonePalettes.resize(offset + draw.boneCount);

                // HLSL이 열 우선으로 읽으므로 전치해 둔다. world 행렬과 같은
                // 규약이다 — 본만 다른 규약으로 올리면 팔다리가 날아간다.
                for (uint32_t i = 0; i < draw.boneCount; ++i)
                {
                    m_bonePalettes[offset + i] = XMMatrixTranspose(draw.bonePalette[i]);
                }

                m_boneOffsets.emplace(draw.animatorKey, offset);
            }
            ++m_lastSkinnedCount;
        }
    }
}

bool EnhancedWireFramePass::PrepareFrame(const EnhancedFrameContext& context,
    std::string& outError)
{
    m_width = context.width;
    m_height = context.height;

    m_batches.clear();
    m_instances.clear();
    m_bonePalettes.clear();
    m_boneOffsets.clear();
    m_geometry.clear();
    m_lastDrawItemCount = 0;
    m_lastBatchCount = 0;
    m_lastSkinnedCount = 0;

    if (nullptr != context.camera)
    {
        m_viewProjection = XMMatrixMultiply(context.camera->view, context.camera->projection);
    }
    else
    {
        m_viewProjection = XMMatrixIdentity();
    }

    // 두 큐를 다 그린다 — 와이어프레임은 deferred/forward 구분이 없다.
    CollectDraws(context.draws);
    CollectDraws(context.forwardDraws);

    if (m_batches.empty()) return true;

    // 배치별 시작 위치를 정하고, 월드 행렬을 배치 순서로 연속 배치한다.
    uint32_t offset = 0;
    for (Batch& batch : m_batches)
    {
        batch.first = offset;
        offset += batch.count;
        batch.count = 0;   // 아래 채우기에서 다시 센다
    }
    m_instances.resize(offset);

    const auto place = [&](const std::vector<EnhancedDrawItem>* draws)
    {
        if (nullptr == draws) return;
        for (const EnhancedDrawItem& draw : *draws)
        {
            if (nullptr == draw.mesh) continue;
            for (Batch& batch : m_batches)
            {
                if (batch.mesh != draw.mesh) continue;

                InstanceData& instance = m_instances[batch.first + batch.count];
                instance.world = XMMatrixTranspose(draw.worldMatrix);

                // 팔레트가 없으면 kNoSkinning으로 남아 셰이더가 건너뛴다.
                instance.boneOffset = kNoSkinning;
                if (nullptr != draw.bonePalette && 0 != draw.boneCount)
                {
                    const auto found = m_boneOffsets.find(draw.animatorKey);
                    if (found != m_boneOffsets.end()) instance.boneOffset = found->second;
                }

                ++batch.count;
                break;
            }
        }
    };
    place(context.draws);
    place(context.forwardDraws);

    // 메시 업로드는 여기서 한다 — Record는 리소스를 만들지 않는다(3-6 규약).
    // GBuffer가 이미 올린 메시면 캐시 히트라 업로드 0회다.
    for (const Batch& batch : m_batches)
    {
        std::string uploadError;
        const auto entry = context.meshCache->GetOrUpload(batch.mesh, uploadError);
        if (!entry.IsValid())
        {
            if (!uploadError.empty())
            {
                outError = "와이어프레임 메시 업로드 실패: " + uploadError;
                return false;
            }
            continue;   // 빈 메시는 건너뛴다
        }
        m_geometry[batch.mesh] = entry;
    }

    m_lastBatchCount = static_cast<uint32_t>(m_batches.size());
    return true;
}

void EnhancedWireFramePass::Declare(EnhancedRenderGraph& graph,
    const EnhancedFrameContext& context)
{
    m_output = RGHandle{};
    m_depth = RGHandle{};

    if (!m_pso.IsValid() ||
        0 == m_width || 0 == m_height)
    {
        return;
    }

    const bool ownsColor = !m_inputs.color.IsValid();
    const bool ownsDepth = !m_inputs.depth.IsValid();

    if (ownsColor)
    {
        RGTextureDesc desc{};
        desc.width = m_width;
        desc.height = m_height;
        desc.format = m_outputFormat;
        desc.allowRenderTarget = true;
        desc.name = "WireFrame.Output";
        m_output = graph.CreateTexture(desc);
    }
    else
    {
        m_output = m_inputs.color;
    }

    if (ownsDepth)
    {
        RGTextureDesc desc{};
        desc.width = m_width;
        desc.height = m_height;
        desc.format = kDepthFormat;
        desc.allowDepthStencil = true;
        desc.name = "WireFrame.Depth";
        m_depth = graph.CreateTexture(desc);
    }
    else
    {
        m_depth = m_inputs.depth;
    }

    std::vector<EnhancedRenderGraph::RGPassUsage> usages;
    usages.push_back({ m_output, RHIResourceState::RenderTarget });
    usages.push_back({ m_depth, RHIResourceState::DepthWrite });

    graph.AddPass(GetName(), usages,
        [this, &context, ownsColor, ownsDepth](
            const EnhancedRenderGraph::ExecuteContext& executeContext)
        {
            RHIEncoder& encoder = *executeContext.encoder;

            const RHITextureHandle colors[] = { executeContext.ResolveHandle(m_output) };
            const auto depthDesc = RHIDepthTargetDesc::Depth(
                executeContext.ResolveHandle(m_depth), kDepthFormat);
            const auto targets = context.resources->CreateRenderTargets(colors, &depthDesc);
            if (!targets.IsValid()) return;

            encoder.SetViewportAndScissor(m_width, m_height);
            encoder.BindRenderTargets(targets);

            if (ownsColor)
            {
                constexpr float kClear[4] = { 0.f, 0.f, 0.f, 0.f };
                encoder.ClearRenderTargets(targets, kClear);
            }
            if (ownsDepth)
            {
                encoder.ClearDepthTarget(targets, 1.f);
            }

            if (m_batches.empty() || m_instances.empty()) return;

            WireFrameConstants constants{};
            constants.viewProjection = XMMatrixTranspose(m_viewProjection);

            const auto cb = context.resources->UploadConstants(
                &constants, sizeof(WireFrameConstants));
            if (!cb.IsValid()) return;
            const uint64_t instanceBytes =
                sizeof(InstanceData) * static_cast<uint64_t>(m_instances.size());
            const auto instanceUpload = context.resources->AllocateUpload(
                RHIUploadRequest{ instanceBytes, RHIUploadUsage::BufferCopy,
                    sizeof(InstanceData) });
            if (!instanceUpload.IsValid()) return;
            memcpy(instanceUpload.cpuAddress, m_instances.data(),
                static_cast<size_t>(instanceBytes));

            // 팔레트가 없어도 t1은 꽂는다. 스킨드가 없는 프레임에서도 루트
            // SRV가 비면 검증 레이어가 잡는다(GBuffer에서 겪은 것과 같다).
            const uint64_t paletteBytes = m_bonePalettes.empty()
                ? sizeof(Mathf::Matrix)
                : sizeof(Mathf::Matrix) * static_cast<uint64_t>(m_bonePalettes.size());

            const auto paletteUpload = context.resources->AllocateUpload(
                RHIUploadRequest{ paletteBytes, RHIUploadUsage::BufferCopy,
                    sizeof(Mathf::Matrix) });
            if (!paletteUpload.IsValid()) return;

            if (m_bonePalettes.empty())
            {
                const Mathf::Matrix identity = XMMatrixIdentity();
                memcpy(paletteUpload.cpuAddress, &identity, sizeof(identity));
            }
            else
            {
                memcpy(paletteUpload.cpuAddress, m_bonePalettes.data(),
                    static_cast<size_t>(paletteBytes));
            }

            encoder.SetPipeline(RHIBindPoint::Graphics, m_pso);
            encoder.SetPrimitiveTopology(RHIPrimitiveTopology::TriangleList);
            encoder.SetConstantBuffer(RHIBindPoint::Graphics, 0, cb);
            encoder.SetRootBuffer(RHIBindPoint::Graphics, 2, paletteUpload);

            for (const Batch& batch : m_batches)
            {
                const auto geometry = m_geometry.find(batch.mesh);
                if (geometry == m_geometry.end()) continue;

                encoder.SetRootBuffer(RHIBindPoint::Graphics, 1,
                    instanceUpload.SubRange(
                        static_cast<uint64_t>(batch.first) * sizeof(InstanceData),
                        static_cast<uint64_t>(batch.count) * sizeof(InstanceData)));

                encoder.SetVertexBuffer(geometry->second.vertices, geometry->second.vertexStride);
                encoder.SetIndexBuffer(geometry->second.indices, geometry->second.indexFormat);
                encoder.DrawIndexed(geometry->second.indexCount, batch.count);
            }
        },
        m_keepAlive);
}

void EnhancedWireFramePass::Shutdown()
{
    m_batches.clear();
    m_instances.clear();
    m_bonePalettes.clear();
    m_boneOffsets.clear();
    m_geometry.clear();
    m_lastDrawItemCount = 0;
    m_lastBatchCount = 0;
    m_lastSkinnedCount = 0;
    m_width = 0;
    m_height = 0;
    m_viewProjection = XMMatrixIdentity();

    m_pso = {};
}
