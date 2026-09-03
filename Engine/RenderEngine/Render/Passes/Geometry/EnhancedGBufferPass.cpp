#include "EnhancedGBufferPass.h"
#include "../../Graph/EnhancedDrawIdentity.h" // I6-C
#include "../../../Assets/ModelVertexLayout.h"
#include "../../../RHI/ModelVertexInputLayout.h"
#include "../../../RHI/RHIShaderCompiler.h"
#include "../../../RHI/RHIShaderSource.h"
#include "../../../ShaderMeta.h"
#include "../../../ShaderMetaReflection.h"
#include "../../../ShaderPermutationDomain.h"
#include "../../../StandardMaterialProperty.h"
#include "../../../RHI/DX12/DX12DeviceResources.h"
#include "../../../RHI/DX12/DX12PSOManager.h"
#include "../../../RHI/DX12/DX12RootSignatureCache.h"
#include "../../../RHI/DX12/DX12MeshCache.h"
#include "../../../RHI/DX12/DX12TextureCache.h"
#include "../../../Mesh.h"
#include "../../../Texture.h"
#include "../../../RHI/RHIEncoder.h"

#include <algorithm>
#include <functional>
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

    // M6-P1a 전환 중 snapshot이 없는 격리 selftest/legacy draw가 제품 HLSL의
    // b2를 비워 두지 않게 하는 sentinel이다. alphaCutoff < 0이면 shader가
    // InstanceData의 기존 factor를 사용한다. 제품 BuildDrawPool은 이 경로를
    // 쓰지 않고 항상 reflection-packed snapshot을 만든다.
    struct LegacyMaterialConstants
    {
        float baseColor[4]{ 1.f, 1.f, 1.f, 1.f };
        float metallic{ 0.f };
        float roughness{ 1.f };
        float normalScale{ 1.f };
        float occlusionStrength{ 1.f };
        float emissive[3]{ 1.f, 1.f, 1.f };
        float alphaCutoff{ -1.f };
    };
    static_assert(sizeof(LegacyMaterialConstants) == 48u);
    static_assert(offsetof(LegacyMaterialConstants, alphaCutoff) == 44u);
    constexpr LegacyMaterialConstants kLegacyMaterialConstants{};

    constexpr std::uint32_t kGBufferMaterialTextureFirstRegister = 0u;
    constexpr std::uint32_t kGBufferMaterialTextureSlotCount = 4u;

    bool ValidateGBufferTextureLayout(const ShaderMetaBindingLayout& layout,
        std::string& outError)
    {
        std::array<bool, kGBufferMaterialTextureSlotCount> occupied{};
        std::vector<std::string_view> names;
        for (const ShaderMetaPropertyBinding& binding : layout.properties)
        {
            if (ShaderPropertyType::Texture2D != binding.propertyType) continue;
            if (binding.name.empty()
                || RHIShaderResourceKind::Texture != binding.resourceKind
                || 0u != binding.registerSpace
                || binding.registerIndex >= kGBufferMaterialTextureFirstRegister
                    + kGBufferMaterialTextureSlotCount)
            {
                outError = "GBuffer texture property가 t0..t3/space0 범위 밖이다: "
                    + binding.name;
                return false;
            }
            const std::size_t slot = binding.registerIndex
                - kGBufferMaterialTextureFirstRegister;
            if (occupied[slot]
                || std::find(names.begin(), names.end(), binding.name) != names.end())
            {
                outError = "GBuffer texture property 이름/register가 중복이다: "
                    + binding.name;
                return false;
            }
            occupied[slot] = true;
            names.push_back(binding.name);
        }
        return true;
    }

    bool ValidateGBufferTextureSnapshot(const EnhancedMaterialDrawSnapshot& snapshot,
        std::string& outError)
    {
        const std::size_t reflectedTextureCount = static_cast<std::size_t>(std::count_if(
            snapshot.bindingLayout.properties.begin(), snapshot.bindingLayout.properties.end(),
            [](const ShaderMetaPropertyBinding& binding)
            {
                return ShaderPropertyType::Texture2D == binding.propertyType;
            }));
        if (reflectedTextureCount != snapshot.textureBindings.size())
        {
            outError = "GBuffer draw texture owner 수가 reflection schema와 다르다";
            return false;
        }

        std::array<bool, kGBufferMaterialTextureSlotCount> occupied{};
        std::vector<std::string_view> names;
        for (const EnhancedMaterialTextureBinding& texture : snapshot.textureBindings)
        {
            const auto reflected = std::find_if(snapshot.bindingLayout.properties.begin(),
                snapshot.bindingLayout.properties.end(), [&texture](const auto& binding)
                {
                    return binding.name == texture.propertyName;
                });
            if (texture.propertyName.empty()
                || reflected == snapshot.bindingLayout.properties.end()
                || ShaderPropertyType::Texture2D != reflected->propertyType
                || RHIShaderResourceKind::Texture != reflected->resourceKind
                || reflected->registerIndex != texture.registerIndex
                || reflected->registerSpace != texture.registerSpace
                || 0u != texture.registerSpace
                || texture.registerIndex >= kGBufferMaterialTextureFirstRegister
                    + kGBufferMaterialTextureSlotCount)
            {
                outError = "GBuffer draw texture binding이 reflection register와 다르다: ";
                outError += texture.propertyName;
                return false;
            }
            const std::size_t slot = texture.registerIndex
                - kGBufferMaterialTextureFirstRegister;
            if (occupied[slot]
                || std::find(names.begin(), names.end(), texture.propertyName) != names.end())
            {
                outError = "GBuffer draw texture 이름/register가 중복이다: "
                    + texture.propertyName;
                return false;
            }
            occupied[slot] = true;
            names.push_back(texture.propertyName);
        }
        return true;
    }

    bool CompileGBufferShader(const char* shaderFile, const char* entry, const char* target,
        const RHIShaderPermutation& permutation,
        RHIShaderBlob& outBlob, std::string& outError)
    {
        return RHIShaderCompiler::CompileFile(shaderFile, entry, target, permutation,
            outBlob, outError);
    }
}

bool EnhancedGBufferPass::MaterialKey::operator==(const MaterialKey& other) const
{
    if (textures != other.textures
        || static_cast<bool>(snapshot) != static_cast<bool>(other.snapshot))
    {
        return false;
    }
    if (!snapshot) return true;
    return snapshot->shaderMetaHandle == other.snapshot->shaderMetaHandle
        && snapshot->permutationKey == other.snapshot->permutationKey
        && snapshot->keywordSelections == other.snapshot->keywordSelections
        && snapshot->propertyBytes == other.snapshot->propertyBytes;
}

bool EnhancedGBufferPass::MaterialKey::operator<(const MaterialKey& other) const
{
    const ShaderMetaHandle leftHandle = snapshot
        ? snapshot->shaderMetaHandle : ShaderMetaHandle{};
    const ShaderMetaHandle rightHandle = other.snapshot
        ? other.snapshot->shaderMetaHandle : ShaderMetaHandle{};
    if (leftHandle.slot != rightHandle.slot)
        return leftHandle.slot < rightHandle.slot;
    if (leftHandle.generation != rightHandle.generation)
        return leftHandle.generation < rightHandle.generation;
    if (snapshot && other.snapshot)
    {
        if (snapshot->keywordSelections != other.snapshot->keywordSelections)
            return snapshot->keywordSelections < other.snapshot->keywordSelections;
        if (snapshot->permutationKey.hi != other.snapshot->permutationKey.hi)
            return snapshot->permutationKey.hi < other.snapshot->permutationKey.hi;
        if (snapshot->permutationKey.lo != other.snapshot->permutationKey.lo)
            return snapshot->permutationKey.lo < other.snapshot->permutationKey.lo;
        if (snapshot->propertyBytes != other.snapshot->propertyBytes)
            return snapshot->propertyBytes < other.snapshot->propertyBytes;
    }
    return std::lexicographical_compare(textures.begin(), textures.end(),
        other.textures.begin(), other.textures.end(), std::less<Texture*>{});
}

EnhancedGBufferPass::MaterialKey EnhancedGBufferPass::MakeMaterialKey(
    const EnhancedDrawItem& draw) const
{
    MaterialKey key{};
    if (draw.materialSnapshot && draw.materialSnapshot->IsValid())
    {
        for (const EnhancedMaterialTextureBinding& binding :
            draw.materialSnapshot->textureBindings)
        {
            if (binding.registerSpace != 0u
                || binding.registerIndex >= kGBufferMaterialTextureFirstRegister
                    + kGBufferMaterialTextureSlotCount)
            {
                continue;
            }
            key.textures[binding.registerIndex
                - kGBufferMaterialTextureFirstRegister] = binding.textureOwner.get();
        }
        key.snapshot = draw.materialSnapshot;
    }
    else
    {
        key.textures = {
            draw.baseColor, draw.normalMap, draw.occRoughMetal, draw.emissive };
    }
    return key;
}

bool EnhancedGBufferPass::ResolveShaderVariant(
    const EnhancedMaterialDrawSnapshot& snapshot,
    uint32_t vertexAttributeMask, RHIPipelineHandle& outPipeline,
    std::shared_ptr<const ShaderMetaBindingLayout>& outLayout) const
{
    // I5-D34a/b: 마스크가 0이 아니면 experiment 레이아웃 짝(core/skin)을 준다.
    // 짝이 없으면 실패다 — legacy PSO로 폴백하면 96B 레이아웃 PSO에 packed
    // 버퍼가 물려 화면이 조용히 틀린다(fail-closed).
    const bool experiment = 0 != vertexAttributeMask;
    const auto resolveModelPipeline = [vertexAttributeMask](
        const RHIGraphicsPipelineRequest& core,
        const RHIGraphicsPipelineRequest& color,
        const RHIGraphicsPipelineRequest& skin,
        const RHIGraphicsPipelineRequest& colorSkin)
    {
        if (vertexAttributeMask == assets::kCoreVertexAttributes)
            return core.GetHandle();
        if (vertexAttributeMask == (assets::kCoreVertexAttributes
            | assets::kColorVertexAttributes)) return color.GetHandle();
        if (vertexAttributeMask == (assets::kCoreVertexAttributes
            | assets::kSkinVertexAttributes)) return skin.GetHandle();
        if (vertexAttributeMask == assets::kCoreColorSkinVertexAttributes)
            return colorSkin.GetHandle();
        return RHIPipelineHandle{};
    };
    if (snapshot.shaderMetaHandle == m_shaderMetaHandle
        && snapshot.permutationKey == m_defaultPermutationKey)
    {
        outPipeline = !experiment ? m_pipelineRequest.GetHandle()
            : resolveModelPipeline(m_experimentPipelineRequest,
                m_experimentColorPipelineRequest,
                m_experimentSkinnedPipelineRequest,
                m_experimentColorSkinnedPipelineRequest);
        outLayout = m_shaderBindingLayout;
        return outPipeline.IsValid() && nullptr != outLayout;
    }

    const ShaderVariantKey key{ snapshot.shaderMetaHandle,
        snapshot.permutationKey };
    const auto found = m_shaderVariants.find(key);
    if (found == m_shaderVariants.end()) return false;
    outPipeline = !experiment ? found->second.request.GetHandle()
        : resolveModelPipeline(found->second.experimentRequest,
            found->second.experimentColorRequest,
            found->second.experimentSkinnedRequest,
            found->second.experimentColorSkinnedRequest);
    outLayout = found->second.layout;
    return outPipeline.IsValid() && nullptr != outLayout;
}

RHIPipelineHandle EnhancedGBufferPass::GetShaderVariantPipeline(
    ShaderMetaHandle handle, RHIShaderPermutationKey permutationKey) const
{
    if (handle == m_shaderMetaHandle && permutationKey == m_defaultPermutationKey)
        return m_pipelineRequest.GetHandle();
    const auto found = m_shaderVariants.find({ handle, permutationKey });
    return found == m_shaderVariants.end()
        ? RHIPipelineHandle{} : found->second.request.GetHandle();
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
        ? context.camera->view * context.camera->projection
        : math::matrix4x4::identity();

    if (nullptr == context.draws || nullptr == context.meshCache) return true;

    // 지오메트리와 재질을 따로 훑는다.
    //
    // 예전에는 메시 중복 제거 블록 안에서 재질까지 올렸는데, 그러면 같은 메시를
    // 다른 재질로 두 번 그릴 때 두 번째 재질이 통째로 건너뛰어진다. 중복 제거의
    // 단위가 둘이 다르다 — 지오메트리는 메시별로, 재질은 재질별로 한 번이다.
    for (const auto& draw : *context.draws)
    {
        if (0 == enhanced_draw::GeometryKey(draw)) continue;

        if (draw.materialSnapshot)
        {
            const EnhancedMaterialDrawSnapshot& material = *draw.materialSnapshot;
            if (!material.IsValid()
                || material.bindingLayout.constantBufferRegister != 2
                || material.bindingLayout.constantBufferSpace != 0)
            {
                outError = "GBuffer draw material snapshot의 b2/ShaderMeta 계약이 invalid다";
                return false;
            }
            RHIPipelineHandle materialPipeline{};
            std::shared_ptr<const ShaderMetaBindingLayout> materialLayout;
            // 이 지점은 메시 업로드 전이라 마스크를 모른다 — 여기의 목적은
            // 재질 레이아웃 검증이므로 legacy 축(0)으로 확인한다. 마스크별
            // PSO 선택은 BuildBatches가 한다.
            if (!ResolveShaderVariant(material, 0, materialPipeline, materialLayout)
                || material.bindingLayout != *materialLayout)
            {
                outError = "GBuffer draw material permutation/layout이 준비된 variant와 다르다";
                return false;
            }
            if (!ValidateGBufferTextureSnapshot(material, outError)) return false;
        }

        if (m_drawGeometry.find(enhanced_draw::GeometryKey(draw)) == m_drawGeometry.end())
        {
            std::string uploadError;
            // I5-D4b: 핸들이 실린 아이템은 legacy Mesh 없이 완결되는 핸들
            // 진입점으로 올린다(키=experiment 자산 신원). mesh 포인터는 정렬·
            // 지오메트리 맵 키로 남는다(은퇴는 D4f).
            const auto entry = draw.modelMeshView.IsComplete()
                ? context.meshCache->GetOrUploadModel(draw.modelMeshView, uploadError)
                : context.meshCache->GetOrUpload(draw.mesh, uploadError);
            if (!entry.IsValid())
            {
                // 빈 메시는 그냥 건너뛴다. 업로드 실패는 알린다 — 조용히 안 그리면
                // '왜 이 오브젝트만 안 보이지'가 된다.
                if (!uploadError.empty()) outError = uploadError;
                continue;
            }

            m_drawGeometry.emplace(enhanced_draw::GeometryKey(draw), entry);
        }
        else if (!m_drawGeometry[enhanced_draw::GeometryKey(draw)].IsValid())
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
            const MaterialKey key = MakeMaterialKey(draw);

            if (m_drawTextures.find(key) == m_drawTextures.end())
            {
                DrawTextures textures{};
                for (uint32_t i = 0; i < 4; ++i)
                {
                    std::string textureError;
                    DX12TextureCache::Entry uploaded{};
                    if (nullptr == key.textures[i] && 2 == i)
                    {
                        uploaded = context.textureCache->GetOrmNeutralTexture(textureError);
                    }
                    else if (nullptr == key.textures[i] && 3 == i)
                    {
                        uploaded = context.textureCache->GetBlackTexture(textureError);
                    }
                    else
                    {
                        uploaded = context.textureCache->GetOrUpload(
                            key.textures[i], textureError);
                    }
                    textures.resources[i] = uploaded.handle;
            textures.formats[i] = uploaded.format;
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
                    m_bonePalettes[offset + i] = math::transpose(draw.bonePalette[i]);
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
        if (0 == enhanced_draw::GeometryKey(draw)) continue;

        const auto geometry = m_drawGeometry.find(enhanced_draw::GeometryKey(draw));
        if (geometry == m_drawGeometry.end() || !geometry->second.IsValid()) continue;

        sorted.push_back(&draw);
    }

    std::stable_sort(sorted.begin(), sorted.end(),
        [this](const EnhancedDrawItem* a, const EnhancedDrawItem* b)
        {
            if (enhanced_draw::GeometryKey(*a) != enhanced_draw::GeometryKey(*b))
                return enhanced_draw::GeometryKey(*a) < enhanced_draw::GeometryKey(*b);

            const MaterialKey keyA = MakeMaterialKey(*a);
            const MaterialKey keyB = MakeMaterialKey(*b);
            return keyA < keyB;
        });

    for (const auto* drawPtr : sorted)
    {
        const auto& draw = *drawPtr;

        const MaterialKey key = MakeMaterialKey(draw);

        if (m_batches.empty()
            || m_batches.back().geometryKey != enhanced_draw::GeometryKey(draw)
            || m_batches.back().material != key)
        {
            DrawBatch batch{};
            batch.geometryKey = enhanced_draw::GeometryKey(draw);
            batch.material = key;
            // I5-D34a: 메시 바인딩의 마스크가 레이아웃 축이다. 배치는 메시별로
            // 갈리므로 마스크가 배치 안에서 섞일 수 없다.
            const auto geometry = m_drawGeometry.find(enhanced_draw::GeometryKey(draw));
            const uint32_t vertexMask = geometry != m_drawGeometry.end()
                ? geometry->second.vertexAttributeMask : 0;
            if (key.snapshot)
            {
                std::shared_ptr<const ShaderMetaBindingLayout> ignoredLayout;
                ResolveShaderVariant(*key.snapshot, vertexMask,
                    batch.pipeline, ignoredLayout);
            }
            else
            {
                if (0 == vertexMask)
                    batch.pipeline = m_pipelineRequest.GetHandle();
                else if (vertexMask == assets::kCoreVertexAttributes)
                    batch.pipeline = m_experimentPipelineRequest.GetHandle();
                else if (vertexMask == (assets::kCoreVertexAttributes
                    | assets::kColorVertexAttributes))
                    batch.pipeline = m_experimentColorPipelineRequest.GetHandle();
                else if (vertexMask == (assets::kCoreVertexAttributes
                    | assets::kSkinVertexAttributes))
                    batch.pipeline = m_experimentSkinnedPipelineRequest.GetHandle();
                else if (vertexMask == assets::kCoreColorSkinVertexAttributes)
                    batch.pipeline = m_experimentColorSkinnedPipelineRequest.GetHandle();
            }
            batch.firstInstance = static_cast<uint32_t>(m_instances.size());
            batch.instanceCount = 0;
            m_batches.push_back(batch);
        }

        InstanceData instance{};
        instance.world = math::transpose(draw.worldMatrix);
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

bool EnhancedGBufferPass::BuildPipelineDesc(const EnhancedFrameContext& context,
    const char* shaderFile, const char* vertexEntry, const char* pixelEntry,
    const ShaderRenderState* renderState,
    const RHIShaderPermutation& permutation, uint32_t experimentMask,
    RHIGraphicsPipelineDesc& outDesc,
    RHIShaderBlob& outVs, RHIShaderBlob& outPs, std::string& outError)
{
    // I5-D34a/b: experiment 짝은 호출자의 퍼뮤테이션 위에 레이아웃 매크로를
    // 얹는다. 키워드 축과 독립인 별도 축이라 여기서 합성한다 — 호출자마다
    // 얹게 하면 하나가 빠뜨렸을 때 화면이 조용히 틀린다. 매크로는 마스크에서
    // 유도한다(스킨 유무) — 마스크와 매크로가 갈리면 레이아웃과 VSIn이
    // 어긋나 PSO 생성이 거부된다.
    RHIShaderPermutation experimentPermutation;
    const RHIShaderPermutation* effectivePermutation = &permutation;
    if (0 != experimentMask)
    {
        experimentPermutation = permutation;
        if (!ModelVertexInput::ApplyShaderPermutation(experimentMask,
                experimentPermutation, outError))
            return false;
        effectivePermutation = &experimentPermutation;
    }

    if (!CompileGBufferShader(shaderFile, vertexEntry, "vs_5_0",
            *effectivePermutation, outVs, outError))
        return false;
    if (!CompileGBufferShader(shaderFile, pixelEntry, "ps_5_0",
            *effectivePermutation, outPs, outError))
        return false;

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
        RHILayout::Cbv(2, RHIShaderVisibility::Pixel),           // b2 — M6 Material property block
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

    outDesc = {};
    if (0 != experimentMask)
    {
        const std::vector<RHIInputElement>* elements =
            ModelVertexInput::ResolveInputElements(experimentMask, outError);
        if (nullptr == elements)
        {
            outError = "GBuffer model 입력 레이아웃 유도 실패: " + outError;
            return false;
        }
        outDesc.inputElements = elements->data();
        outDesc.inputElementCount = static_cast<uint32_t>(elements->size());
    }
    else
    {
        outDesc.inputElements = kInputElements;
        outDesc.inputElementCount = _countof(kInputElements);
    }
    outDesc.vsBytecode = outVs.Data();
    outDesc.vsSize = outVs.Size();
    outDesc.psBytecode = outPs.Data();
    outDesc.psSize = outPs.Size();
    outDesc.layout = root;
    outDesc.depthEnable = true;
    outDesc.cullMode = RHICullMode::None;
    outDesc.numRenderTargets = kRenderTargetCount;
    for (uint32_t i = 0; i < kRenderTargetCount; ++i)
    {
        outDesc.rtvFormats[i] = GetRenderTargetFormat(i);
    }
    outDesc.dsvFormat = kDepthFormat;

    if (nullptr != renderState) renderState->ApplyTo(outDesc);
    return true;
}

bool EnhancedGBufferPass::CreatePipeline(const EnhancedFrameContext& context, std::string& outError)
{
    RHIShaderBlob vsBlob;
    RHIShaderBlob psBlob;
    RHIGraphicsPipelineDesc desc{};
    const RHIShaderPermutation emptyPermutation;
    if (!BuildPipelineDesc(context, kGBufferShaderFile, "VSMain", "PSMain", nullptr,
            emptyPermutation, 0, desc, vsBlob, psBlob, outError)) return false;

    if (!m_pipelineRequest.Create(*context.psoManager, desc, outError))
        return false;

    // I5-D34a/b: experiment 레이아웃 짝 둘(core / core+skin). 하나라도 없으면
    // 초기화 실패다 — 배치가 마스크로 고르는 자리에서 폴백을 지어내지 않기
    // 위해서다.
    RHIGraphicsPipelineRequest* requests[] = {
        &m_experimentPipelineRequest,
        &m_experimentColorPipelineRequest,
        &m_experimentSkinnedPipelineRequest,
        &m_experimentColorSkinnedPipelineRequest,
    };
    for (std::size_t i = 0; i < assets::kModelVertexMasks.size(); ++i)
    {
        // desc가 shader blob을 빌리므로 mask마다 blob 수명을 Create까지 보존한다.
        RHIShaderBlob modelVsBlob;
        RHIShaderBlob modelPsBlob;
        RHIGraphicsPipelineDesc modelDesc{};
        if (!BuildPipelineDesc(context, kGBufferShaderFile, "VSMain", "PSMain",
                nullptr, emptyPermutation, assets::kModelVertexMasks[i], modelDesc,
                modelVsBlob, modelPsBlob, outError)
            || !requests[i]->Create(*context.psoManager, modelDesc, outError))
        {
            return false;
        }
    }
    return true;
}

bool EnhancedGBufferPass::BuildShaderMetaPipelineDesc(
    const EnhancedFrameContext& context, const ShaderMeta& meta,
    std::span<const std::uint16_t> keywordSelections, uint32_t experimentMask,
    RHIGraphicsPipelineDesc& outDesc, RHIShaderBlob& outVs,
    RHIShaderBlob& outPs, RHIShaderPermutationKey& outPermutationKey,
    std::shared_ptr<const ShaderMetaBindingLayout>& outLayout,
    std::string& outError)
{
    const auto passIt = std::find_if(meta.passes.begin(), meta.passes.end(),
        [](const ShaderPassDesc& pass) { return pass.name == "GBuffer"; });
    if (passIt == meta.passes.end())
    {
        outError = "GBuffer ShaderMeta에 GBuffer pass가 없다";
        return false;
    }

    const ShaderPassDesc& pass = *passIt;
    if (pass.IsCompute() || !pass.vertex || !pass.pixel
        || ShaderPassQueue::Opaque != pass.queue)
    {
        outError = "GBuffer ShaderMeta pass는 opaque VS+PS graphics여야 한다";
        return false;
    }

    const std::uint32_t passIndex = static_cast<std::uint32_t>(
        std::distance(meta.passes.begin(), passIt));
    ShaderMetaPermutation permutation;
    if (!ShaderPermutationDomain::Resolve(meta, passIndex, keywordSelections,
            permutation, outError))
    {
        return false;
    }

    std::filesystem::path shaderPath = meta.source;
    if (!meta.originPath.empty())
    {
        std::error_code pathError;
        shaderPath = std::filesystem::relative(meta.ResolveSource(meta.originPath),
            RHIShaderSource::Resolve(""), pathError);
        const auto first = shaderPath.begin();
        if (pathError || shaderPath.empty() || shaderPath.is_absolute()
            || (first != shaderPath.end() && *first == ".."))
        {
            outError = "GBuffer ShaderMeta source가 shader root 밖이다";
            return false;
        }
    }

    const std::string shaderFile = shaderPath.generic_string();
    if (shaderFile.empty() || pass.vertex->entry.empty() || pass.pixel->entry.empty())
    {
        outError = "GBuffer ShaderMeta source/entry가 비었다";
        return false;
    }

    if (!BuildPipelineDesc(context, shaderFile.c_str(), pass.vertex->entry.c_str(),
            pass.pixel->entry.c_str(), &pass.state, permutation.defines,
            experimentMask, outDesc, outVs, outPs, outError))
    {
        return false;
    }

    std::shared_ptr<const ShaderMetaBindingLayout> candidateLayout;
    if (!meta.properties.empty())
    {
        std::vector<RHIShaderReflection> reflections(2);
        if (!RHIShaderCompiler::ReflectFile(shaderFile, pass.vertex->entry, "vs_5_0",
                RHIShaderCompiler::GetOutput(), permutation.defines,
                reflections[0], outError)
            || !RHIShaderCompiler::ReflectFile(shaderFile, pass.pixel->entry, "ps_5_0",
                RHIShaderCompiler::GetOutput(), permutation.defines,
                reflections[1], outError))
        {
            return false;
        }

        ShaderMetaBindingLayout layout;
        if (!ShaderMetaReflection::Resolve(meta, reflections, layout, outError))
            return false;
        if (layout.constantBufferName.empty()
            || 2 != layout.constantBufferRegister
            || 0 != layout.constantBufferSpace
            || 0 == layout.constantBufferByteSize)
        {
            outError = "GBuffer Material property layout은 b2/space0이어야 한다";
            return false;
        }
        if (!ValidateGBufferTextureLayout(layout, outError)) return false;
        candidateLayout = std::make_shared<ShaderMetaBindingLayout>(std::move(layout));
    }

    outPermutationKey = permutation.key;
    outLayout = std::move(candidateLayout);
    return true;
}

bool EnhancedGBufferPass::ApplyShaderMeta(const EnhancedFrameContext& context,
    ShaderMetaHandle handle, const ShaderMeta& meta,
    RHICompletionPoint retireAfter, std::string& outError)
{
    if (!handle.IsValid())
    {
        outError = "GBuffer ShaderMeta generation handle이 비었다";
        return false;
    }
    if (handle == m_shaderMetaHandle) return true;

    RHIShaderBlob vsBlob;
    RHIShaderBlob psBlob;
    RHIGraphicsPipelineDesc desc{};
    RHIShaderPermutationKey defaultPermutationKey{};
    std::shared_ptr<const ShaderMetaBindingLayout> candidateLayout;
    const std::vector<std::uint16_t> defaultSelections(meta.keywords.size(), 0);
    if (!BuildShaderMetaPipelineDesc(context, meta, defaultSelections, 0,
            desc, vsBlob, psBlob, defaultPermutationKey, candidateLayout, outError))
        return false;

    // I5-D34a/b: experiment 짝 desc 둘을 후보 단계에서 먼저 완성한다 — 하나라도
    // 못 만들면 아무것도 교체하지 않는 것이 candidate-first 규약이다.
    // ★ 블롭은 desc마다 분리한다. desc는 블롭 내부 버퍼를 빌려 가리키므로,
    //   같은 변수를 재사용하면 앞의 desc가 뒤의 바이트코드(또는 재할당으로
    //   죽은 메모리)를 가리켜 PSO 생성이 E_INVALIDARG로 죽는다 — 실제로 FT
    //   라이브 전멸(드로우 0)로 나타났던 결함이다.
    RHIShaderBlob experimentVsBlob;
    RHIShaderBlob experimentPsBlob;
    RHIGraphicsPipelineDesc experimentDesc{};
    RHIShaderPermutationKey experimentKeyIgnored{};
    std::shared_ptr<const ShaderMetaBindingLayout> experimentLayoutIgnored;
    if (!BuildShaderMetaPipelineDesc(context, meta, defaultSelections,
            assets::kCoreVertexAttributes,
            experimentDesc, experimentVsBlob, experimentPsBlob,
            experimentKeyIgnored, experimentLayoutIgnored, outError))
        return false;

    RHIShaderBlob experimentColorVsBlob;
    RHIShaderBlob experimentColorPsBlob;
    RHIGraphicsPipelineDesc experimentColorDesc{};
    if (!BuildShaderMetaPipelineDesc(context, meta, defaultSelections,
            assets::kCoreVertexAttributes | assets::kColorVertexAttributes,
            experimentColorDesc, experimentColorVsBlob, experimentColorPsBlob,
            experimentKeyIgnored, experimentLayoutIgnored, outError))
        return false;

    RHIShaderBlob experimentSkinnedVsBlob;
    RHIShaderBlob experimentSkinnedPsBlob;
    RHIGraphicsPipelineDesc experimentSkinnedDesc{};
    if (!BuildShaderMetaPipelineDesc(context, meta, defaultSelections,
            assets::kCoreVertexAttributes | assets::kSkinVertexAttributes,
            experimentSkinnedDesc, experimentSkinnedVsBlob,
            experimentSkinnedPsBlob,
            experimentKeyIgnored, experimentLayoutIgnored, outError))
        return false;

    RHIShaderBlob experimentColorSkinnedVsBlob;
    RHIShaderBlob experimentColorSkinnedPsBlob;
    RHIGraphicsPipelineDesc experimentColorSkinnedDesc{};
    if (!BuildShaderMetaPipelineDesc(context, meta, defaultSelections,
            assets::kCoreColorSkinVertexAttributes,
            experimentColorSkinnedDesc, experimentColorSkinnedVsBlob,
            experimentColorSkinnedPsBlob, experimentKeyIgnored,
            experimentLayoutIgnored, outError))
        return false;

    const RHIPipelineHandle previousPipeline = m_pipelineRequest.GetHandle();
    const bool previousShared = std::any_of(m_shaderVariants.begin(),
        m_shaderVariants.end(), [previousPipeline](const auto& entry)
        {
            return entry.second.request.GetHandle() == previousPipeline;
        });
    // 후보가 완성된 뒤에만 기존 handle을 targeted retire한다. 실패하면 현재
    // request와 m_shaderMetaHandle은 그대로라 다음 draw가 끊기지 않는다. 다른
    // meta key가 같은 cache handle을 공유하면 그 holder가 사라질 때까지 보존한다.
    if (!m_pipelineRequest.Replace(*context.psoManager, desc, retireAfter,
            outError, !previousShared))
        return false;

    // I5-D34a/b: experiment 짝 둘도 같은 규약으로 교체한다. desc는 위에서 이미
    // 완성됐으므로 여기 실패는 PSO 생성 실패뿐이고, 그때 앞쪽만 새것인 반쪽
    // 상태가 되지만 실패 반환으로 프레임이 이 meta를 쓰지 않는다.
    const RHIPipelineHandle previousExperiment =
        m_experimentPipelineRequest.GetHandle();
    const bool previousExperimentShared = std::any_of(m_shaderVariants.begin(),
        m_shaderVariants.end(), [previousExperiment](const auto& entry)
        {
            return entry.second.experimentRequest.GetHandle()
                == previousExperiment;
        });
    if (!m_experimentPipelineRequest.Replace(*context.psoManager, experimentDesc,
            retireAfter, outError, !previousExperimentShared))
        return false;

    const RHIPipelineHandle previousExperimentColor =
        m_experimentColorPipelineRequest.GetHandle();
    const bool previousExperimentColorShared = std::any_of(m_shaderVariants.begin(),
        m_shaderVariants.end(), [previousExperimentColor](const auto& entry)
        {
            return entry.second.experimentColorRequest.GetHandle()
                == previousExperimentColor;
        });
    if (!m_experimentColorPipelineRequest.Replace(*context.psoManager,
            experimentColorDesc, retireAfter, outError,
            !previousExperimentColorShared))
        return false;

    const RHIPipelineHandle previousExperimentSkinned =
        m_experimentSkinnedPipelineRequest.GetHandle();
    const bool previousExperimentSkinnedShared = std::any_of(
        m_shaderVariants.begin(), m_shaderVariants.end(),
        [previousExperimentSkinned](const auto& entry)
        {
            return entry.second.experimentSkinnedRequest.GetHandle()
                == previousExperimentSkinned;
        });
    if (!m_experimentSkinnedPipelineRequest.Replace(*context.psoManager,
            experimentSkinnedDesc, retireAfter, outError,
            !previousExperimentSkinnedShared))
        return false;

    const RHIPipelineHandle previousExperimentColorSkinned =
        m_experimentColorSkinnedPipelineRequest.GetHandle();
    const bool previousExperimentColorSkinnedShared = std::any_of(
        m_shaderVariants.begin(), m_shaderVariants.end(),
        [previousExperimentColorSkinned](const auto& entry)
        {
            return entry.second.experimentColorSkinnedRequest.GetHandle()
                == previousExperimentColorSkinned;
        });
    if (!m_experimentColorSkinnedPipelineRequest.Replace(*context.psoManager,
            experimentColorSkinnedDesc, retireAfter, outError,
            !previousExperimentColorSkinnedShared))
        return false;

    // primary generation 교체는 같은 catalog slot의 옛 permutation만 지운다.
    // 다른 material ShaderMeta slot은 현재 frame sealing이 성공한 뒤 Commit에서
    // 판단한다. 같은 cache handle을 공유하는 holder가 남아 있으면 무효화하지 않는다.
    std::vector<RHIPipelineHandle> removedPipelines;
    const std::uint32_t previousSlot = m_shaderMetaHandle.slot;
    for (auto it = m_shaderVariants.begin(); it != m_shaderVariants.end();)
    {
        if (0 != previousSlot && it->first.meta.slot == previousSlot)
        {
            removedPipelines.push_back(it->second.request.GetHandle());
            removedPipelines.push_back(
                it->second.experimentRequest.GetHandle());
            removedPipelines.push_back(
                it->second.experimentColorRequest.GetHandle());
            removedPipelines.push_back(
                it->second.experimentSkinnedRequest.GetHandle());
            removedPipelines.push_back(
                it->second.experimentColorSkinnedRequest.GetHandle());
            it = m_shaderVariants.erase(it);
        }
        else
        {
            ++it;
        }
    }
    m_shaderMetaHandle = handle;
    m_defaultPermutationKey = defaultPermutationKey;
    m_shaderBindingLayout = std::move(candidateLayout);

    std::vector<std::uint32_t> invalidated;
    for (const RHIPipelineHandle pipeline : removedPipelines)
    {
        if (!pipeline.IsValid() || pipeline == m_pipelineRequest.GetHandle()
            || pipeline == m_experimentPipelineRequest.GetHandle()
            || pipeline == m_experimentColorPipelineRequest.GetHandle()
            || pipeline == m_experimentSkinnedPipelineRequest.GetHandle()
            || pipeline == m_experimentColorSkinnedPipelineRequest.GetHandle()
            || std::find(invalidated.begin(), invalidated.end(), pipeline.id)
                != invalidated.end()
            || std::any_of(m_shaderVariants.begin(), m_shaderVariants.end(),
                [pipeline](const auto& entry)
                {
                    return entry.second.request.GetHandle() == pipeline
                        || entry.second.experimentRequest.GetHandle()
                            == pipeline
                        || entry.second.experimentColorRequest.GetHandle()
                            == pipeline
                        || entry.second.experimentSkinnedRequest.GetHandle()
                            == pipeline
                        || entry.second.experimentColorSkinnedRequest.GetHandle()
                            == pipeline;
                }))
        {
            continue;
        }
        context.psoManager->InvalidatePipeline(pipeline, retireAfter);
        invalidated.push_back(pipeline.id);
    }
    return true;
}

bool EnhancedGBufferPass::EnsureShaderMetaVariant(
    const EnhancedFrameContext& context, ShaderMetaHandle handle,
    const ShaderMeta& meta,
    std::span<const std::uint16_t> keywordSelections,
    RHIShaderPermutationKey& outPermutationKey,
    std::shared_ptr<const ShaderMetaBindingLayout>& outLayout,
    std::string& outError)
{
    if (!handle.IsValid())
    {
        outError = "GBuffer material variant의 ShaderMeta generation이 비었다";
        return false;
    }

    const auto passIt = std::find_if(meta.passes.begin(), meta.passes.end(),
        [](const ShaderPassDesc& pass) { return pass.name == "GBuffer"; });
    if (passIt == meta.passes.end())
    {
        outError = "GBuffer material variant에 GBuffer pass가 없다";
        return false;
    }
    const std::uint32_t passIndex = static_cast<std::uint32_t>(
        std::distance(meta.passes.begin(), passIt));
    ShaderMetaPermutation resolved;
    if (!ShaderPermutationDomain::Resolve(meta, passIndex, keywordSelections,
            resolved, outError))
    {
        return false;
    }

    outPermutationKey = resolved.key;
    if (handle == m_shaderMetaHandle && resolved.key == m_defaultPermutationKey)
    {
        outLayout = m_shaderBindingLayout;
        return nullptr != outLayout && m_pipelineRequest.IsValid();
    }

    const ShaderVariantKey key{ handle, resolved.key };
    const auto existing = m_shaderVariants.find(key);
    if (existing != m_shaderVariants.end())
    {
        outLayout = existing->second.layout;
        return existing->second.request.IsValid() && nullptr != outLayout;
    }

    RHIShaderBlob vsBlob;
    RHIShaderBlob psBlob;
    RHIGraphicsPipelineDesc desc{};
    RHIShaderPermutationKey candidateKey{};
    std::shared_ptr<const ShaderMetaBindingLayout> candidateLayout;
    if (!BuildShaderMetaPipelineDesc(context, meta, keywordSelections, 0,
            desc, vsBlob, psBlob, candidateKey, candidateLayout, outError))
    {
        return false;
    }
    if (candidateKey != resolved.key || !candidateLayout)
    {
        outError = "GBuffer material variant의 permutation/layout identity가 불완전하다";
        return false;
    }
    if (!m_pipelineRequest.IsValid()
        || desc.layout != m_pipelineRequest.GetDesc().layout)
    {
        outError = "GBuffer material ShaderMeta pipeline layout이 primary pass와 다르다";
        return false;
    }

    // I5-D34a/b: experiment 짝 desc 둘. variant 생성 시점에는 어떤 메시가 이
    // 재질로 그려질지 모르므로 셋을 함께 만든다 — 배치가 메시 마스크로 고른다.
    // ★ 블롭 분리 — ApplyShaderMeta와 같은 이유(위 참조). 앞의 desc가 빌려
    //   가리키는 버퍼를 덮으면 안 된다.
    RHIShaderBlob experimentVsBlob;
    RHIShaderBlob experimentPsBlob;
    RHIGraphicsPipelineDesc experimentDesc{};
    RHIShaderPermutationKey experimentKeyIgnored{};
    std::shared_ptr<const ShaderMetaBindingLayout> experimentLayoutIgnored;
    if (!BuildShaderMetaPipelineDesc(context, meta, keywordSelections,
            assets::kCoreVertexAttributes,
            experimentDesc, experimentVsBlob, experimentPsBlob,
            experimentKeyIgnored, experimentLayoutIgnored, outError))
    {
        return false;
    }
    RHIShaderBlob experimentColorVsBlob;
    RHIShaderBlob experimentColorPsBlob;
    RHIGraphicsPipelineDesc experimentColorDesc{};
    if (!BuildShaderMetaPipelineDesc(context, meta, keywordSelections,
            assets::kCoreVertexAttributes | assets::kColorVertexAttributes,
            experimentColorDesc, experimentColorVsBlob, experimentColorPsBlob,
            experimentKeyIgnored, experimentLayoutIgnored, outError))
    {
        return false;
    }
    RHIShaderBlob experimentSkinnedVsBlob;
    RHIShaderBlob experimentSkinnedPsBlob;
    RHIGraphicsPipelineDesc experimentSkinnedDesc{};
    if (!BuildShaderMetaPipelineDesc(context, meta, keywordSelections,
            assets::kCoreVertexAttributes | assets::kSkinVertexAttributes,
            experimentSkinnedDesc, experimentSkinnedVsBlob,
            experimentSkinnedPsBlob,
            experimentKeyIgnored, experimentLayoutIgnored, outError))
    {
        return false;
    }
    RHIShaderBlob experimentColorSkinnedVsBlob;
    RHIShaderBlob experimentColorSkinnedPsBlob;
    RHIGraphicsPipelineDesc experimentColorSkinnedDesc{};
    if (!BuildShaderMetaPipelineDesc(context, meta, keywordSelections,
            assets::kCoreColorSkinVertexAttributes,
            experimentColorSkinnedDesc, experimentColorSkinnedVsBlob,
            experimentColorSkinnedPsBlob, experimentKeyIgnored,
            experimentLayoutIgnored, outError))
    {
        return false;
    }

    ShaderVariant candidate;
    candidate.layout = candidateLayout;
    if (!candidate.request.Create(*context.psoManager, desc, outError)) return false;
    if (!candidate.experimentRequest.Create(*context.psoManager, experimentDesc,
            outError))
        return false;
    if (!candidate.experimentColorRequest.Create(*context.psoManager,
            experimentColorDesc, outError))
        return false;
    if (!candidate.experimentSkinnedRequest.Create(*context.psoManager,
            experimentSkinnedDesc, outError))
        return false;
    if (!candidate.experimentColorSkinnedRequest.Create(*context.psoManager,
            experimentColorSkinnedDesc, outError))
        return false;

    auto [inserted, accepted] = m_shaderVariants.emplace(key, std::move(candidate));
    if (!accepted)
    {
        outError = "GBuffer material variant cache insert가 충돌했다";
        return false;
    }
    outLayout = inserted->second.layout;
    outPermutationKey = candidateKey;
    outError.clear();
    return true;
}

std::uint32_t EnhancedGBufferPass::CommitShaderMetaFrame(
    const EnhancedFrameContext& context,
    std::span<const ShaderMetaHandle> activeHandles,
    RHICompletionPoint retireAfter)
{
    if (nullptr == context.psoManager) return 0;
    const auto isActive = [activeHandles](ShaderMetaHandle handle)
    {
        return std::find(activeHandles.begin(), activeHandles.end(), handle)
            != activeHandles.end();
    };

    std::vector<RHIPipelineHandle> removedPipelines;
    std::uint32_t removedKeys = 0;
    for (auto it = m_shaderVariants.begin(); it != m_shaderVariants.end();)
    {
        if (!isActive(it->first.meta))
        {
            removedPipelines.push_back(it->second.request.GetHandle());
            removedPipelines.push_back(
                it->second.experimentRequest.GetHandle());
            removedPipelines.push_back(
                it->second.experimentColorRequest.GetHandle());
            removedPipelines.push_back(
                it->second.experimentSkinnedRequest.GetHandle());
            removedPipelines.push_back(
                it->second.experimentColorSkinnedRequest.GetHandle());
            it = m_shaderVariants.erase(it);
            ++removedKeys;
        }
        else
        {
            ++it;
        }
    }

    std::vector<std::uint32_t> invalidated;
    for (const RHIPipelineHandle pipeline : removedPipelines)
    {
        if (!pipeline.IsValid() || pipeline == m_pipelineRequest.GetHandle()
            || pipeline == m_experimentPipelineRequest.GetHandle()
            || pipeline == m_experimentColorPipelineRequest.GetHandle()
            || pipeline == m_experimentSkinnedPipelineRequest.GetHandle()
            || pipeline == m_experimentColorSkinnedPipelineRequest.GetHandle()
            || std::find(invalidated.begin(), invalidated.end(), pipeline.id)
                != invalidated.end()
            || std::any_of(m_shaderVariants.begin(), m_shaderVariants.end(),
                [pipeline](const auto& entry)
                {
                    return entry.second.request.GetHandle() == pipeline
                        || entry.second.experimentRequest.GetHandle()
                            == pipeline
                        || entry.second.experimentColorRequest.GetHandle()
                            == pipeline
                        || entry.second.experimentSkinnedRequest.GetHandle()
                            == pipeline
                        || entry.second.experimentColorSkinnedRequest.GetHandle()
                            == pipeline;
                }))
        {
            continue;
        }
        context.psoManager->InvalidatePipeline(pipeline, retireAfter);
        invalidated.push_back(pipeline.id);
    }
    return removedKeys;
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

            // 프레임/팔레트 상수를 걸기 전에 공용 root layout을 설치한다.
            // batch loop는 같은 layout의 permutation PSO만 바꾸므로 DX12Encoder와
            // VulkanEncoder 모두 이미 건 root/descriptor 상태를 보존한다.
            encoder.SetPipeline(RHIBindPoint::Graphics, m_pipelineRequest.GetHandle());
            encoder.SetPrimitiveTopology(RHIPrimitiveTopology::TriangleList);

            // 프레임 상수는 한 번만 올린다. 드로우마다 올리면 같은 값을 수백 번
            // 복사하는 꼴이고, 그건 CE 단계를 늘리는 방향이다.
            //
            // HLSL은 행 우선으로 읽으므로 전치해서 넣는다.
            const math::matrix4x4 viewProjection = math::transpose(m_frameViewProjection);
            const auto frameConstants = context.resources->UploadConstants(
                &viewProjection, sizeof(viewProjection));
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
                    ? sizeof(math::matrix4x4)
                    : sizeof(math::matrix4x4) * static_cast<uint64_t>(m_bonePalettes.size());

                const auto paletteBuffer = context.resources->AllocateUpload(
                    RHIUploadRequest{ paletteBytes, RHIUploadUsage::BufferCopy,
                        sizeof(math::matrix4x4) });
                if (!paletteBuffer.IsValid()) return;

                if (m_bonePalettes.empty())
                {
                    constexpr math::matrix4x4 identity = math::matrix4x4::identity();
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
                RHIUploadRequest{ sliceInstanceBytes, RHIUploadUsage::BufferCopy,
                    sizeof(InstanceData) });
            if (!instanceBlock.IsValid()) return;   // 구간이 찼다 — 이 조각은 다음 프레임
            memcpy(instanceBlock.cpuAddress, &m_instances[sliceFirstInstance],
                static_cast<size_t>(sliceInstanceBytes));

            for (size_t batchIndex = sliceBegin; batchIndex < sliceEnd; ++batchIndex)
            {
                const DrawBatch& batch = m_batches[batchIndex];
                if (0 == batch.instanceCount || !batch.pipeline.IsValid()) continue;

                const auto mesh = m_drawGeometry.find(batch.geometryKey);
                if (mesh == m_drawGeometry.end() || !mesh->second.IsValid()) continue;

                // M6-P1b2b1: PSO는 pass 전역 한 번이 아니라 material batch 직전에
                // 고른다. 같은 texture/property라도 keyword permutation이 다르면
                // 서로 다른 pipeline handle로 기록된다.
                encoder.SetPipeline(RHIBindPoint::Graphics, batch.pipeline);

                // M6-P1a: property bytes는 batch key의 일부다. 같은 texture/mesh라도
                // 값이 다르면 batch가 갈리고, 그 batch를 기록하기 직전에 b2를
                // 바꾼다. active ShaderMeta와 다른 handle은 PrepareFrame에서 이미
                // fail-closed되어 이 지점에 도달하지 않는다.
                const bool hasSnapshot = batch.material.snapshot
                    && !batch.material.snapshot->propertyBytes.empty();
                const void* materialData = !hasSnapshot
                    ? static_cast<const void*>(&kLegacyMaterialConstants)
                    : static_cast<const void*>(
                        batch.material.snapshot->propertyBytes.data());
                const std::size_t materialSize = !hasSnapshot
                    ? sizeof(kLegacyMaterialConstants)
                    : batch.material.snapshot->propertyBytes.size();
                const auto materialConstants = context.resources->UploadConstants(
                    materialData, materialSize);
                if (!materialConstants.IsValid()) continue;
                encoder.SetConstantBuffer(RHIBindPoint::Graphics, 5, materialConstants);

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
    return (std::max)(1u, (std::min)(IRHIParallelCommandPool::kMaxWorkers, byBatches));
}

void EnhancedGBufferPass::Shutdown()
{
    m_drawGeometry.clear();
    m_drawTextures.clear();
    m_bonePalettes.clear();
    m_boneOffsets.clear();
    m_shaderVariants.clear();
    m_pipelineRequest = {};
    m_experimentPipelineRequest = {};
    m_experimentColorPipelineRequest = {};
    m_experimentSkinnedPipelineRequest = {};
    m_experimentColorSkinnedPipelineRequest = {};
    m_shaderMetaHandle = {};
    m_defaultPermutationKey = {};
    m_shaderBindingLayout.reset();
}
