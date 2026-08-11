#ifndef DYNAMICCPP_EXPORTS
#include "EnhancedGBufferPass.h"
#include "DX12ShaderCompiler.h"
#include "DX12DeviceResources.h"
#include "DX12PSOManager.h"
#include "DX12RootSignatureCache.h"
#include "DX12MeshCache.h"
#include "DX12TextureCache.h"
#include "../../Mesh.h"
#include "../../Texture.h"
#include "../RHIEncoder.h"

#include <algorithm>
#include <sstream>

namespace
{
    // 유니티 빌드에서 익명 네임스페이스가 파일 간 합쳐지므로 이름을 고유하게 둔다.
    std::string GBufferHrToString(HRESULT hr)
    {
        std::ostringstream oss;
        oss << "HRESULT 0x" << std::hex << static_cast<unsigned long>(hr);
        return oss.str();
    }

    // 첫 슬라이스의 셰이더는 소스에 담는다. 재질 셰이더 연결은 씬 연결
    // 슬라이스에서 ShaderSystem·PSOManager와 함께 붙인다.
    //
    // 타깃마다 서로 다른 값을 쓰는 이유는 검증 때문이다 — 다섯 타깃이 실제로
    // 각각 기록되는지 확인하려면 값이 구분되어야 한다. 한 타깃만 기록되고
    // 나머지가 비어 있어도 '그려지긴 한다'로 보이는 것을 막는다.

    constexpr const char* kGBufferShaderFile = "GBuffer.hlsl";

    bool CompileGBufferShader(const char* entry, const char* target,
        RHIShaderBlob& outBlob, std::string& outError)
    {
        return DX12ShaderCompiler::CompileFile(kGBufferShaderFile, entry, target,
            outBlob, outError);
    }
}

RHIFormat EnhancedGBufferPass::GetRenderTargetFormat(uint32_t index)
{
    // DX11 쪽 구성과 같아야 대조가 성립한다.
    switch (index)
    {
    case 0: return RHIFormat::RGBA16Float;  // Diffuse
    case 1: return RHIFormat::RGBA16Float;  // MetalRough
    case 2: return RHIFormat::RGBA16Float;  // Normal
    case 3: return RHIFormat::RGBA16Float;  // Emissive
    case 4: return RHIFormat::R32Uint;            // Bitmask
    default: return RHIFormat::Unknown;
    }
}

bool EnhancedGBufferPass::PrepareFrame(const EnhancedFrameContext& context, std::string& outError)
{
    m_drawGeometry.clear();
    m_drawTextures.clear();
    m_bonePalettes.clear();
    m_boneOffsets.clear();
    m_lastDrawCount = 0;
    m_lastSkinnedCount = 0;

    // 프레임 밀봉된 카메라에서 뷰·투영을 만든다. 스냅샷이 없으면 항등으로 두는데,
    // 그러면 클립 공간에 바로 그리게 되므로 '카메라가 안 붙었다'가 화면에 드러난다.
    m_frameViewProjection = (nullptr != context.camera)
        ? XMMatrixMultiply(context.camera->view, context.camera->projection)
        : XMMatrixIdentity();

    if (nullptr == context.draws || nullptr == context.meshCache) return true;

    // 지오메트리와 재질을 따로 훑는다.
    //
    // 예전에는 메시 중복 제거 블록 안에서 재질까지 올렸는데, 그러면 같은 메시를
    // 다른 재질로 두 번 그릴 때 두 번째 재질이 통째로 건너뛰어진다. 중복 제거의
    // 단위가 둘이 다르다 — 지오메트리는 메시별로, 재질은 재질별로 한 번이다.
    for (const auto& draw : *context.draws)
    {
        if (nullptr == draw.mesh) continue;

        if (m_drawGeometry.find(draw.mesh) == m_drawGeometry.end())
        {
            std::string uploadError;
            const auto entry = context.meshCache->GetOrUpload(draw.mesh, uploadError);
            if (!entry.IsValid())
            {
                // 빈 메시는 그냥 건너뛴다. 업로드 실패는 알린다 — 조용히 안 그리면
                // '왜 이 오브젝트만 안 보이지'가 된다.
                if (!uploadError.empty()) outError = uploadError;
                continue;
            }

            m_drawGeometry.emplace(draw.mesh, entry);
        }
        else if (!m_drawGeometry[draw.mesh].IsValid())
        {
            continue;
        }

        // 재질 텍스처. 없는 슬롯은 폴백이 채워 항상 넷이 채워지고, 셰이더에
        // 분기가 필요 없다.
        //
        // ★ 폴백은 슬롯 의미를 따라간다. 흰색 하나로 전부 때우면 emissive는
        //   전부 자체발광이 되고, ORM은 B(금속)가 1이라 확산이 통째로 죽는다 —
        //   IBL 소비 검증의 '끔=검정' 대조군이 실측으로 잡았다.
        if (nullptr != context.textureCache)
        {
            const MaterialKey key{
                draw.baseColor, draw.normalMap, draw.occRoughMetal, draw.emissive };

            if (m_drawTextures.find(key) == m_drawTextures.end())
            {
                DrawTextures textures{};
                for (uint32_t i = 0; i < 4; ++i)
                {
                    std::string textureError;
                    DX12TextureCache::Entry uploaded{};
                    if (nullptr == key[i] && 2 == i)
                    {
                        uploaded = context.textureCache->GetOrmNeutralTexture(textureError);
                    }
                    else if (nullptr == key[i] && 3 == i)
                    {
                        uploaded = context.textureCache->GetBlackTexture(textureError);
                    }
                    else
                    {
                        uploaded = context.textureCache->GetOrUpload(key[i], textureError);
                    }
                    textures.resources[i] = uploaded.handle;
                    textures.formats[i] = FromDXGI(uploaded.format);
                    textures.mipLevels[i] = uploaded.mipLevels;

                    if (!textureError.empty()) outError = textureError;
                }
                m_drawTextures.emplace(key, textures);
            }
        }

        // 본 팔레트를 애니메이터별로 한 번씩만 담는다.
        //
        // 한 캐릭터의 메시가 여럿이면 프록시도 여럿인데 팔레트는 하나다 —
        // 중복 제거가 없으면 같은 512행렬(32KB)을 메시 수만큼 올린다.
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

        ++m_lastDrawCount;
    }

    m_lastMeshCount = static_cast<uint32_t>(m_drawGeometry.size());
    m_lastMaterialCount = static_cast<uint32_t>(m_drawTextures.size());

    BuildBatches(context);

    return true;
}

void EnhancedGBufferPass::BuildBatches(const EnhancedFrameContext& context)
{
    m_instances.clear();
    m_batches.clear();
    m_lastBatchCount = 0;

    if (nullptr == context.draws) return;

    m_instances.reserve(context.draws->size());

    // 같은 (메시, 재질)을 묶는다.
    //
    // 둘 중 하나라도 다르면 같은 드로우로 묶을 수 없다 — 메시가 다르면 정점·
    // 인덱스 버퍼를, 재질이 다르면 SRV 테이블을 바꿔야 하기 때문이다.
    //
    // ★ 정렬해야 실제로 묶인다.
    //
    // 처음에는 '연속한 같은 것'만 묶었다. 순서를 흔들면 깊이 테스트 결과가
    // 달라질까 봐서였는데, 재 보니 배치 수가 드로우 수와 똑같았다(704 → 704) —
    // 씬의 드로우 순서는 대개 메시가 번갈아 나오므로 연속이 거의 없다.
    // 병합이 통째로 죽어 있었고, 수치를 안 봤으면 몰랐을 것이다.
    //
    // 정렬해도 되는 이유: GBuffer는 불투명만 그리고 깊이 함수가 LESS다.
    // 겹치는 픽셀은 더 가까운 쪽이 이기므로 그리는 순서와 무관하다.
    // (같은 깊이면 순서를 타지만 그건 원래 불안정한 경우다.)
    //
    // 투명 재질이 들어오면 이 전제가 깨진다 — 그때는 불투명만 정렬하고
    // 투명은 뒤에서 앞으로 따로 그려야 한다.
    std::vector<const EnhancedDrawItem*> sorted;
    sorted.reserve(context.draws->size());
    for (const auto& draw : *context.draws)
    {
        if (nullptr == draw.mesh) continue;

        const auto geometry = m_drawGeometry.find(draw.mesh);
        if (geometry == m_drawGeometry.end() || !geometry->second.IsValid()) continue;

        sorted.push_back(&draw);
    }

    std::stable_sort(sorted.begin(), sorted.end(),
        [](const EnhancedDrawItem* a, const EnhancedDrawItem* b)
        {
            if (a->mesh != b->mesh) return a->mesh < b->mesh;

            const MaterialKey keyA{ a->baseColor, a->normalMap, a->occRoughMetal, a->emissive };
            const MaterialKey keyB{ b->baseColor, b->normalMap, b->occRoughMetal, b->emissive };
            return keyA < keyB;
        });

    for (const auto* drawPtr : sorted)
    {
        const auto& draw = *drawPtr;

        const MaterialKey key{
            draw.baseColor, draw.normalMap, draw.occRoughMetal, draw.emissive };

        if (m_batches.empty() || m_batches.back().mesh != draw.mesh
            || m_batches.back().material != key)
        {
            DrawBatch batch{};
            batch.mesh = draw.mesh;
            batch.material = key;
            batch.firstInstance = static_cast<uint32_t>(m_instances.size());
            batch.instanceCount = 0;
            m_batches.push_back(batch);
        }

        InstanceData instance{};
        instance.world = XMMatrixTranspose(draw.worldMatrix);
        instance.baseColorFactor = draw.baseColorFactor;
        instance.metallic = draw.metallic;
        instance.roughness = draw.roughness;
        instance.useNormalMap = draw.useNormalMap;

        // 스키닝 오프셋. 팔레트가 없으면 kNoSkinning으로 남아 셰이더가
        // 바인드 포즈로 그린다 — 스킨드와 비스킨드가 한 배치에 섞여도 된다.
        instance.boneOffset = kNoSkinning;
        if (nullptr != draw.bonePalette && 0 != draw.boneCount)
        {
            const auto found = m_boneOffsets.find(draw.animatorKey);
            if (found != m_boneOffsets.end()) instance.boneOffset = found->second;
        }

        m_instances.push_back(instance);
        ++m_batches.back().instanceCount;
    }

    m_lastBatchCount = static_cast<uint32_t>(m_batches.size());
}

bool EnhancedGBufferPass::CreatePipeline(const EnhancedFrameContext& context, std::string& outError)
{
    RHIShaderBlob vsBlob;
    RHIShaderBlob psBlob;
    if (!CompileGBufferShader("VSMain", "vs_5_0", vsBlob, outError)) return false;
    if (!CompileGBufferShader("PSMain", "ps_5_0", psBlob, outError)) return false;

    // 루트 시그니처는 캐시가 식별자를 준다 — 손번호를 붙이지 않는 것이 3-4의 계약이다.
    //
    // 상수는 디스크립터 테이블이 아니라 루트 CBV로 넘긴다. 업로드 링에서 자른
    // 조각의 GPU 주소를 그대로 꽂으면 되므로 디스크립터를 만들 필요가 없고,
    // 드로우마다 바뀌는 값에는 이쪽이 싸다(테이블은 디스크립터 힙을 거친다).
    // 인스턴스 버퍼와 본 팔레트는 루트 SRV로 넘긴다. 디스크립터를 만들 필요
    // 없이 업로드 링의 GPU 주소를 그대로 꽂으면 되고, 본 팔레트는 프레임당
    // 한 번이면 배치가 몇 개든 그대로 쓴다 — 인스턴스가 자기 오프셋을
    // 들고 있어서다.
    const RHIPipelineLayoutParam params[] = {
        RHILayout::Cbv(0, RHIShaderVisibility::Vertex),          // b0 — 프레임 상수
        RHILayout::Srv(4, RHIShaderVisibility::Vertex),          // t4 — 인스턴스 데이터
        RHILayout::SrvTable(4, 0, RHIShaderVisibility::Pixel),   // baseColor · normal · occRoughMetal · emissive
        RHILayout::SamplerTable(1, 0, RHIShaderVisibility::Pixel),
        RHILayout::Srv(5, RHIShaderVisibility::Vertex),          // t5 — 본 팔레트
    };

    RHIPipelineLayoutDesc rootDesc{};
    rootDesc.params = params;
    rootDesc.allowInputAssembler = true;

    const auto root = context.rootSignatures->GetOrCreate(rootDesc, outError);
    if (!root.IsValid()) return false;

    // 입력 레이아웃. 정점 구조체와 순서가 맞아야 하고, 어긋나면 검증 레이어가
    // 잡아 주지 않는 경우도 있어 화면이 조용히 이상해진다.
    // 오프셋은 엔진 Vertex 구조체를 그대로 따른다:
    //   position 0 · normal 12 · uv0 24 · uv1 32 · tangent 40 · bitangent 52
    //   · boneIndices 64 · boneWeights 80
    // 어긋나면 검증 레이어가 잡아 주지 않는 경우도 있어 화면이 조용히 이상해진다.
    static const RHIInputElement kInputElements[] = {
        { "POSITION",     0, RHIFormat::RGB32Float,    0,  0, 0 },
        { "NORMAL",       0, RHIFormat::RGB32Float,    0, 12, 0 },
        { "TEXCOORD",     0, RHIFormat::RG32Float,       0, 24, 0 },
        { "TANGENT",      0, RHIFormat::RGB32Float,    0, 40, 0 },
        { "BINORMAL",     0, RHIFormat::RGB32Float,    0, 52, 0 },
        // 본 인덱스가 float4인 것은 엔진 Vertex를 그대로 따르는 것이다.
        // UINT4로 읽으면 float 비트를 정수로 해석해 팔레트 밖을 짚는다.
        { "BLENDINDICES", 0, RHIFormat::RGBA32Float, 0, 64, 0 },
        { "BLENDWEIGHT",  0, RHIFormat::RGBA32Float, 0, 80, 0 },
    };

    // 오프셋이 Vertex와 어긋나면 조용히 틀리므로 컴파일 시점에 못박는다.
    static_assert(offsetof(Vertex, normal) == 12, "Vertex 레이아웃이 바뀌었다 — 입력 요소 오프셋을 맞출 것");
    static_assert(offsetof(Vertex, uv0) == 24, "Vertex 레이아웃이 바뀌었다");
    static_assert(offsetof(Vertex, tangent) == 40, "Vertex 레이아웃이 바뀌었다");
    static_assert(offsetof(Vertex, bitangent) == 52, "Vertex 레이아웃이 바뀌었다");
    static_assert(offsetof(Vertex, boneIndices) == 64, "Vertex 레이아웃이 바뀌었다");
    static_assert(offsetof(Vertex, boneWeights) == 80, "Vertex 레이아웃이 바뀌었다");

    RHIGraphicsPipelineDesc desc{};
    desc.inputElements = kInputElements;
    desc.inputElementCount = _countof(kInputElements);
    desc.vsBytecode = vsBlob.Data();
    desc.vsSize = vsBlob.Size();
    desc.psBytecode = psBlob.Data();
    desc.psSize = psBlob.Size();
    desc.layout = root;
    desc.depthEnable = true;
    desc.cullMode = RHICullMode::None;
    desc.numRenderTargets = kRenderTargetCount;
    for (uint32_t i = 0; i < kRenderTargetCount; ++i)
    {
        desc.rtvFormats[i] = GetRenderTargetFormat(i);
    }
    desc.dsvFormat = kDepthFormat;

    m_pso = context.psoManager->GetOrCreate(desc, outError);
    return m_pso.IsValid();
}

bool EnhancedGBufferPass::Initialize(const EnhancedFrameContext& context, std::string& outError)
{
    if (nullptr == context.resources || nullptr == context.psoManager ||
        nullptr == context.rootSignatures)
    {
        outError = "GBuffer 패스 컨텍스트가 불완전하다";
        return false;
    }

    if (!CreatePipeline(context, outError)) return false;

    const RHISamplerDesc sampler = RHISampler::Linear(RHIAddressMode::Wrap);

    m_sampler = context.resources->CreateSamplers({ &sampler, 1 });
    if (!m_sampler.IsValid())
    {
        outError = "GBuffer 샘플러 생성 실패";
        return false;
    }

    return true;
}

void EnhancedGBufferPass::Declare(EnhancedRenderGraph& graph, const EnhancedFrameContext& context)
{
    // 타깃을 그래프에 선언한다. 실제 생성과 배리어는 그래프가 맡는다 —
    // 이 패스는 무엇을 어떤 상태로 쓸지만 말한다.
    static const char* kNames[kRenderTargetCount] = {
        "GBuffer.Diffuse", "GBuffer.MetalRough", "GBuffer.Normal",
        "GBuffer.Emissive", "GBuffer.Bitmask" };

    RGHandle targets[kRenderTargetCount]{};
    for (uint32_t i = 0; i < kRenderTargetCount; ++i)
    {
        RGTextureDesc desc{};
        desc.width = context.width;
        desc.height = context.height;
        desc.format = GetRenderTargetFormat(i);
        desc.allowRenderTarget = true;
        desc.name = kNames[i];
        targets[i] = graph.CreateTexture(desc);
    }

    RGTextureDesc depthDesc{};
    depthDesc.width = context.width;
    depthDesc.height = context.height;
    depthDesc.format = kDepthFormat;
    depthDesc.allowDepthStencil = true;
    depthDesc.name = "GBuffer.Depth";
    const RGHandle depth = graph.CreateTexture(depthDesc);

    m_outputs.diffuse = targets[0];
    m_outputs.metalRough = targets[1];
    m_outputs.normal = targets[2];
    m_outputs.emissive = targets[3];
    m_outputs.bitmask = targets[4];
    m_outputs.depth = depth;

    std::vector<EnhancedRenderGraph::RGPassUsage> usages;
    usages.reserve(kRenderTargetCount + 1);
    for (uint32_t i = 0; i < kRenderTargetCount; ++i)
    {
        usages.push_back({ targets[i], RHIResourceState::RenderTarget });
    }
    usages.push_back({ depth, RHIResourceState::DepthWrite });

    // 소비자가 없을 때만 뿌리로 표시해 살려 둔다(SetKeepAlive).
    // Deferred가 붙으면 그쪽이 읽으므로 표시 없이도 살아남아야 한다.
    const bool keepAlive = m_keepAlive;

    // 쪼갤 수 있는 패스로 선언한다.
    //
    // 이 패스가 기록 시간의 대부분을 차지한다 — 패스 단위 병렬화만으로는
    // '가장 무거운 패스'보다 빨라질 수 없어 1.25배에서 평평했다.
    //
    // 조각 상한은 워커 상한과 맞춘다. 그보다 잘게 쪼개도 같은 워커가 연달아
    // 맡게 되고, 그러면 조각마다 상태를 다시 거는 비용만 늘어난다.
    graph.AddSplitPass(GetName(), usages,
        [this, &context, targets, depth](const EnhancedRenderGraph::ExecuteContext& executeContext,
            uint32_t slice, uint32_t sliceCount)
        {
            RHIEncoder& encoder = *executeContext.encoder;

            // 뷰는 매 프레임 만든다. 그래프가 리소스를 프레임마다 다르게 줄 수
            // 있으므로(컬링·앨리어싱) 캐시하면 어긋난다.
            //
            // ★ 조각마다 다시 만든다. 조각들이 워커에 흩어져 동시에 기록하므로
            //   한 벌을 나눠 쓰면 만드는 쪽과 거는 쪽이 어긋날 수 있다 —
            //   프레임 힙에서 조각별로 잘라 오면 그 경합 자체가 없다.
            RHITextureHandle colors[kRenderTargetCount]{};
            for (uint32_t i = 0; i < kRenderTargetCount; ++i)
            {
                colors[i] = executeContext.ResolveHandle(targets[i]);
            }

            const auto depthDesc = RHIDepthTargetDesc::Depth(
                executeContext.ResolveHandle(depth), kDepthFormat);
            const auto boundTargets = context.resources->CreateRenderTargets(colors, &depthDesc);
            if (!boundTargets.IsValid()) return;

            encoder.SetViewportAndScissor(context.width, context.height);

            encoder.BindRenderTargets(boundTargets);

            // 클리어 값은 0으로 둔다. 그려진 곳과 안 그려진 곳이 값으로 구분되어야
            // 픽셀 검증이 '다섯 타깃 각각이 실제로 기록됐는가'를 볼 수 있다.
            //
            // 클리어는 첫 조각에서만 한다. 조각들은 순서대로 실행되므로 뒤
            // 조각이 또 지우면 앞 조각이 그린 것이 사라진다.
            constexpr float kZero[4] = { 0.f, 0.f, 0.f, 0.f };
            if (0 == slice)
            {
                encoder.ClearRenderTargets(boundTargets, kZero);
                encoder.ClearDepthTarget(boundTargets, 1.f);
            }

            encoder.SetPipeline(RHIBindPoint::Graphics, m_pso);
            encoder.SetPrimitiveTopology(RHIPrimitiveTopology::TriangleList);

            // 프레임 상수는 한 번만 올린다. 드로우마다 올리면 같은 값을 수백 번
            // 복사하는 꼴이고, 그건 CE 단계를 늘리는 방향이다.
            //
            // HLSL은 행 우선으로 읽으므로 전치해서 넣는다.
            const Mathf::Matrix viewProjection = XMMatrixTranspose(m_frameViewProjection);
            const auto frameConstants = context.resources->UploadConstants(
                &viewProjection, sizeof(Mathf::Matrix));
            if (!frameConstants.IsValid()) return;
            encoder.SetConstantBuffer(RHIBindPoint::Graphics, 0, frameConstants);

            // 그릴 것이 없으면 클리어만 하고 끝난다 — 빈 씬도 정상 경로다.
            if (nullptr == context.draws) return;

            encoder.SetSamplers(RHIBindPoint::Graphics, 3, m_sampler);

            // ── 본 팔레트 ──
            //
            // 배치 밖에서 한 번 올린다. 애니메이터가 몇이든 인스턴스가 자기
            // 오프셋을 들고 있으므로 배치마다 다시 꽂을 이유가 없다 — DX11이
            // 애니메이터마다 cbuffer를 다시 올리며 드로우를 끊던 자리다.
            //
            // 팔레트가 없어도 t5는 꽂는다. 스킨드가 없는 프레임에서도
            // 루트 SRV가 비면 셰이더가 읽지 않더라도 검증 레이어가 경고하고,
            // 조각(slice)마다 상태를 다시 걸어야 하므로 여기가 그 자리다.
            {
                const uint64_t paletteBytes = m_bonePalettes.empty()
                    ? sizeof(Mathf::Matrix)
                    : sizeof(Mathf::Matrix) * static_cast<uint64_t>(m_bonePalettes.size());

                const auto paletteBuffer = context.resources->AllocateUpload(
                    paletteBytes, sizeof(Mathf::Matrix));
                if (!paletteBuffer.IsValid()) return;

                if (m_bonePalettes.empty())
                {
                    const Mathf::Matrix identity = XMMatrixIdentity();
                    memcpy(paletteBuffer.cpuAddress, &identity, sizeof(identity));
                }
                else
                {
                    memcpy(paletteBuffer.cpuAddress, m_bonePalettes.data(),
                        static_cast<size_t>(paletteBytes));
                }

                encoder.SetRootBuffer(RHIBindPoint::Graphics, 4, paletteBuffer);
            }

            // 자기 몫의 배치만 그린다.
            //
            // 단위가 드로우가 아니라 배치다. 같은 메시·재질을 쓰는 드로우들이
            // 인스턴스 하나로 묶여 DrawIndexedInstanced 한 번에 나간다 —
            // 드로우마다 상수를 올리고 SRV 테이블을 만들던 것이 배치마다 한 번이 된다.
            //
            // 조각들은 선언 순서대로 실행되므로 결과는 통째로 그린 것과 같다.
            const size_t batchCount = m_batches.size();
            const size_t sliceBegin = batchCount * slice / sliceCount;
            const size_t sliceEnd = batchCount * (slice + 1) / sliceCount;
            if (sliceBegin >= sliceEnd) return;

            // ── 조각의 인스턴스를 블록 하나로 올린다 (일괄 할당) ──
            //
            // 예전에는 배치마다 링에서 잘랐다. dx12.bench11 실측이 그 비용을
            // 정확히 보여 줬다 — Allocate 한 번이 원자 연산 몇 개를 지나며
            // 호출당 ~175ns, 드로우당 할당 3.568ms가 일괄 5.25배(0.679ms)로
            // 줄었다. 배치들의 인스턴스는 m_instances에 배치 순서로 연속이라
            // (PrepareFrame의 계약), 조각 구간 전체가 한 번의 memcpy로 올라간다.
            // 배치마다 갈리는 것은 루트 SRV 주소 하나다.
            const uint32_t sliceFirstInstance = m_batches[sliceBegin].firstInstance;
            const DrawBatch& sliceLastBatch = m_batches[sliceEnd - 1];
            const uint32_t sliceInstanceEnd =
                sliceLastBatch.firstInstance + sliceLastBatch.instanceCount;
            if (sliceInstanceEnd <= sliceFirstInstance) return;

            const uint64_t sliceInstanceBytes = sizeof(InstanceData)
                * static_cast<uint64_t>(sliceInstanceEnd - sliceFirstInstance);
            const auto instanceBlock = context.resources->AllocateUpload(
                sliceInstanceBytes, sizeof(InstanceData));
            if (!instanceBlock.IsValid()) return;   // 구간이 찼다 — 이 조각은 다음 프레임
            memcpy(instanceBlock.cpuAddress, &m_instances[sliceFirstInstance],
                static_cast<size_t>(sliceInstanceBytes));

            for (size_t batchIndex = sliceBegin; batchIndex < sliceEnd; ++batchIndex)
            {
                const DrawBatch& batch = m_batches[batchIndex];
                if (0 == batch.instanceCount) continue;

                const auto mesh = m_drawGeometry.find(batch.mesh);
                if (mesh == m_drawGeometry.end() || !mesh->second.IsValid()) continue;

                encoder.SetRootBuffer(RHIBindPoint::Graphics, 1,
                    instanceBlock.SubRange(
                        sizeof(InstanceData)
                            * static_cast<uint64_t>(batch.firstInstance - sliceFirstInstance),
                        sizeof(InstanceData) * batch.instanceCount));

                // 재질 텍스처 넷을 연속으로 잘라 테이블 하나로 묶는다.
                // PrepareFrame이 올려 둔 것을 쓴다 — 기록 중에는 만들지 않는다.
                const auto textures = m_drawTextures.find(batch.material);
                if (textures != m_drawTextures.end())
                {
                    const DrawTextures& t = textures->second;
                    const RHIBindingDesc srvs[] = {
                        RHIBindingDesc::Srv2D(t.resources[0], t.formats[0], 0, t.mipLevels[0]),
                        RHIBindingDesc::Srv2D(t.resources[1], t.formats[1], 0, t.mipLevels[1]),
                        RHIBindingDesc::Srv2D(t.resources[2], t.formats[2], 0, t.mipLevels[2]),
                        RHIBindingDesc::Srv2D(t.resources[3], t.formats[3], 0, t.mipLevels[3]),
                    };
                    const RHIBindingTable srvTable = context.resources->CreateBindings(srvs);

                    // ★ 넷 중 하나라도 없으면 이 배치를 건너뛴다.
                    //
                    // 예전에는 널인 슬롯만 건너뛰고 나머지로 테이블을 걸었다.
                    // 그러면 그 칸에 힙의 이전 내용이 남은 채로 셰이더가 읽는다 —
                    // 화면에는 '가끔 엉뚱한 텍스처'로만 나타나고 검증 레이어도
                    // 조용하다. PrepareFrame이 폴백으로 넷을 채우므로 널은
                    // 업로드 실패뿐이고, 그때는 안 그리는 편이 낫다.
                    if (!srvTable.IsValid()) continue;

                    encoder.SetBindings(RHIBindPoint::Graphics, 2, srvTable);
                }

                encoder.SetVertexBuffer(mesh->second.vertices, mesh->second.vertexStride);
                encoder.SetIndexBuffer(mesh->second.indices, mesh->second.indexFormat);
                encoder.DrawIndexed(mesh->second.indexCount, batch.instanceCount);
            }
        },
        // 조각 수는 드로우 수로 정한다.
        //
        // ★ 상한만 두고 무조건 쪼갰더니 작은 씬에서 오히려 느려졌다.
        // 실측: 드로우 44에서 1.23배(분할 없음) → 0.71배(6조각)로 뒤집혔다.
        // 조각마다 상태를 다시 걸어야 하기 때문이다 — RTV 5개와 DSV를 만들고
        // 뷰포트·루트 시그니처·PSO·힙을 세우고 프레임 상수를 올린다. 그 비용이
        // 드로우 일곱 개 그리는 것보다 크다.
        //
        // 그래서 조각당 최소 드로우 수를 둔다. 그 아래로는 쪼개지 않는다.
        ComputeSliceCount(),
        keepAlive,
        // ★ 기록량은 배치 수다. 드로우 수가 아니다.
        //
        // 처음에는 인스턴스 수 + 배치 수로 뒀다. 드로우마다 컬링과 인스턴스
        // 수집이 도니 그쪽이 비용을 따를 거라고 봤는데, 재 보니 아니었다.
        // 같은 드로우 704를 배치 11로 묶으면 순차 0.34 ms, 배치 704로 흩으면
        // 1.25~2.08 ms다 — 네 배 넘게 차이 난다. 인스턴스를 배열에 밀어 넣는
        // 일은 싸고, 배치마다 디스크립터·버퍼·PSO를 다시 거는 일이 비싸다.
        //
        // 병렬 이득도 배치를 따라간다: 배치 11이면 드로우 11264에서도
        // 0.95~1.52배로 갈리고, 배치 704면 드로우 704에서 이미 1.43~2.10배다.
        m_lastBatchCount);
}

uint32_t EnhancedGBufferPass::ComputeSliceCount() const
{
    // 조각당 최소 드로우 수. 실측으로 정했다 — 드로우 44를 6조각으로 나누면
    // 조각당 일곱이고 그때 상태 설정 비용이 이겼다. 176(조각당 29)부터는
    // 분할이 이겼으므로 그 사이 어딘가가 경계다.
    constexpr uint32_t kMinDrawsPerSlice = 32;

    // 기준은 드로우가 아니라 배치다. 인스턴싱으로 묶인 뒤에는 배치 수가
    // 기록 비용을 결정한다 — 드로우 700개가 배치 11개로 묶였다면 쪼갤 것이 없다.
    if (m_lastBatchCount <= kMinDrawsPerSlice) return 1;

    const uint32_t byBatches = m_lastBatchCount / kMinDrawsPerSlice;
    return (std::max)(1u, (std::min)(DX12CommandListPool::kMaxWorkers, byBatches));
}

void EnhancedGBufferPass::Shutdown()
{
    m_drawGeometry.clear();
    m_drawTextures.clear();
    m_bonePalettes.clear();
    m_boneOffsets.clear();
    m_pso = {};
}

#endif
