#include "EnhancedForwardPass.h"
#include "../../../RHI/DX12/DX12DeviceResources.h"
#include "../../../RHI/DX12/DX12PSOManager.h"
#include "../../../RHI/DX12/DX12RootSignatureCache.h"
#include "../../Graph/EnhancedRenderGraph.h"
#include "../../../RHI/RHIEncoder.h"
#include "../../../RHI/RHIShaderSource.h"
#include "../../Scene/EnhancedSceneRenderer.h"

// ★ 자기가 쓰는 것은 자기가 포함한다.
//
// 전에는 이 둘이 없어도 빌드가 됐다. 유니티 빌드가 같은 덩어리에 묶은
// 다른 파일이 끌어와 주고 있었기 때문이다. 파일 하나를 프로젝트에 더한
// 것만으로 묶음이 바뀌어 갑자기 'Vertex는 선언되지 않은 식별자'가 났다.
// 남의 include에 얹혀 있으면 이런 식으로 무관한 변경에서 터진다.
#include "../../../RHI/DX12/DX12MeshCache.h"
#include "../../../Mesh.h"
#include "../../../Texture.h"
#include "../../../ShaderMeta.h"
#include "../../../ShaderMetaReflection.h"
#include "../../../ShaderPermutationDomain.h"
#include "../../../StandardMaterialProperty.h"

#include <algorithm>
#include <filesystem>
#include <functional>
#include <sstream>
#include <string_view>
#include <vector>
#include "../../../RHI/RHIShaderCompiler.h"

// 남은 단계(순서대로 채운다):
//   [v] 1. 광원 컬링 컴퓨트 — 타일 프러스텀 vs 광원 구, 타일 목록 쓰기
//   [v] 2. 컬링 자가 검증 — 타일 카운트 리드백, 알려진 배치로 단정
//   [v] 3. 포워드 셰이딩 — draws 드로우 + 타일 목록 조회
//   [v] 4. 참조 경로(전 광원 루프)와 픽셀 대조 — EnhancedForwardShadeTest.cpp
//   [v] 5. 광원 수 스케일링 실측 — EnhancedForwardScaleTest.cpp
//
// 다섯 단계가 다 섰고 실제 씬(dx12.scene)에도 붙었다. 씬에서의 실측:
//   불투명만  — 컬링 0.0031 ms · 셰이딩 생략(포워드 큐 비어 있음)
//   투명 4건  — 컬링 0.0031 ms · 셰이딩 0.184 ms

namespace
{
    // 유니티 빌드에서 익명 네임스페이스가 파일 간 합쳐지므로 이름을 고유하게 둔다.
    std::string FwdHrToString(HRESULT hr)
    {
        std::ostringstream oss;
        oss << "HRESULT 0x" << std::hex << static_cast<unsigned long>(hr);
        return oss.str();
    }

    // ── 광원 컬링 ──
    //
    // 스레드 그룹 하나가 타일 하나다(16x16 = 256스레드). 세 단계:
    //   ① 각 스레드가 자기 픽셀의 깊이를 읽어 그룹 공유 min/max에 모은다.
    //      깊이는 float이지만 InterlockedMin/Max는 uint만 받는다 — 양수
    //      float은 비트 패턴의 대소가 값의 대소와 같으므로 asuint로 겪는다.
    //   ② 타일 프러스텀을 만든다. 옆면 4개는 뷰 공간에서 원점을 지나는
    //      평면이라 타일 코너의 뷰 방향 둘의 외적이 곧 법선이다.
    //   ③ 광원을 256개씩 나눠 병렬로 검사한다. 통과하면 공유 카운터로
    //      슬롯을 받아 목록에 쓴다.
    //
    // 하늘만 있는 타일은 광원을 받지 않는다 — min/max가 비어 있으면 어떤
    // 표면도 없다는 뜻이고, 그 타일의 픽셀 셰이더는 어차피 그릴 것이 없다.
    constexpr const char* kCullShaderFile = "ForwardCull.hlsl";

    // ── 포워드 셰이딩 ──
    //
    // Forward+의 핵심이 여기 한 줄이다:
    //
    //   for (i = 0; i < gTileCount[tile]; ++i)   ← 전체 광원이 아니라 타일 목록
    //
    // 기존 포워드는 픽셀마다 gLightCount를 돌았다. 여기서는 자기 타일에
    // 닿는 광원만 돈다. 씬 광원이 수백 개여도 타일당 몇 개면 그만큼만 돈다.
    //
    // 참조 경로(REFERENCE_PATH)는 같은 셰이더에서 매크로로 갈린다. 전 광원을
    // 도는 옛 방식이고, 픽셀 대조의 정답지 역할을 한다 — 컬링이 광원을
    // 잘못 떨어뜨리면 두 결과가 갈린다. 셰이더를 따로 쓰면 조명 계산 자체가
    // 달라져 무엇 때문에 다른지 알 수 없으므로, 광원을 고르는 부분만 다르게
    // 하고 나머지는 같은 코드를 쓴다.
    constexpr const char* kShadeShaderFile = "ForwardShade.hlsl";

    // snapshot이 없는 격리 selftest는 기존 ShadeInstance scalar를 쓰되 b2를
    // 비워 두지 않는다. 제품 draw는 reflection-packed 48B Standard prefix와
    // 재질별 numeric tail을 포함한 가변 block을 사용한다.
    struct ForwardLegacyMaterialConstants
    {
        float baseColor[4]{ 1.f, 1.f, 1.f, 1.f };
        float metallic{ 0.f };
        float roughness{ 1.f };
        float normalScale{ 1.f };
        float occlusionStrength{ 1.f };
        float emissive[3]{ 1.f, 1.f, 1.f };
        float alphaCutoff{ 0.f };
    };
    static_assert(sizeof(ForwardLegacyMaterialConstants) == 48u);
    static_assert(offsetof(ForwardLegacyMaterialConstants, alphaCutoff) == 44u);
    constexpr ForwardLegacyMaterialConstants kForwardLegacyMaterialConstants{};

    constexpr std::uint32_t kForwardMaterialTextureFirstRegister = 4u;
    constexpr std::uint32_t kForwardMaterialTextureSlotCount = 4u;

    struct ForwardNumericPrefixBinding
    {
        std::string_view name;
        ShaderPropertyType type;
        std::uint32_t byteOffset;
        std::uint32_t byteSize;
    };

    // Water/Wind처럼 숫자 tail을 추가하는 Forward 재질도 첫 48B는 Standard
    // property ABI를 그대로 유지한다. 이 prefix 덕분에 공통 pass가 재질별
    // cbuffer 크기를 허용하면서도 같은 이름을 다른 offset으로 해석하지 않는다.
    constexpr std::array kForwardNumericPrefix{
        ForwardNumericPrefixBinding{ standard_material::property::BaseColor,
            ShaderPropertyType::Float4, 0u, 16u },
        ForwardNumericPrefixBinding{ standard_material::property::Metallic,
            ShaderPropertyType::Float, 16u, 4u },
        ForwardNumericPrefixBinding{ standard_material::property::Roughness,
            ShaderPropertyType::Float, 20u, 4u },
        ForwardNumericPrefixBinding{ standard_material::property::NormalScale,
            ShaderPropertyType::Float, 24u, 4u },
        ForwardNumericPrefixBinding{ standard_material::property::OcclusionStrength,
            ShaderPropertyType::Float, 28u, 4u },
        ForwardNumericPrefixBinding{ standard_material::property::Emissive,
            ShaderPropertyType::Float3, 32u, 12u },
        ForwardNumericPrefixBinding{ standard_material::property::AlphaCutoff,
            ShaderPropertyType::Float, 44u, 4u },
    };
    constexpr std::uint32_t kForwardNumericPrefixByteSize = 48u;
    constexpr std::uint32_t kMaxForwardMaterialConstantBytes = 64u * 1024u;

    bool ValidateForwardNumericLayout(const ShaderMetaBindingLayout& layout,
        std::string& outError)
    {
        if (layout.constantBufferName != "MaterialProperties"
            || 2u != layout.constantBufferRegister
            || 0u != layout.constantBufferSpace
            || layout.constantBufferByteSize < kForwardNumericPrefixByteSize
            || layout.constantBufferByteSize > kMaxForwardMaterialConstantBytes
            || 0u != (layout.constantBufferByteSize % 16u))
        {
            outError = "Forward Material property layout은 "
                "MaterialProperties b2/space0, 48B 이상 16B 정렬이어야 한다";
            return false;
        }

        for (const ForwardNumericPrefixBinding& expected : kForwardNumericPrefix)
        {
            const auto found = std::find_if(layout.properties.begin(),
                layout.properties.end(), [&expected](const ShaderMetaPropertyBinding& binding)
                {
                    return binding.name == expected.name;
                });
            if (found == layout.properties.end()
                || found->propertyType != expected.type
                || RHIShaderResourceKind::ConstantBuffer != found->resourceKind
                || found->resourceName != layout.constantBufferName
                || 2u != found->registerIndex || 0u != found->registerSpace
                || found->byteOffset != expected.byteOffset
                || found->byteSize != expected.byteSize)
            {
                outError = "Forward Standard numeric prefix reflection 불일치: ";
                outError += expected.name;
                return false;
            }
        }

        bool hasReflectedTail = false;
        for (const ShaderMetaPropertyBinding& binding : layout.properties)
        {
            if (ShaderPropertyType::Texture2D == binding.propertyType) continue;
            const bool isPrefix = std::any_of(kForwardNumericPrefix.begin(),
                kForwardNumericPrefix.end(), [&binding](const ForwardNumericPrefixBinding& expected)
                {
                    return binding.name == expected.name;
                });
            const std::uint64_t byteEnd = static_cast<std::uint64_t>(binding.byteOffset)
                + static_cast<std::uint64_t>(binding.byteSize);
            if (RHIShaderResourceKind::ConstantBuffer != binding.resourceKind
                || binding.resourceName != layout.constantBufferName
                || 2u != binding.registerIndex || 0u != binding.registerSpace
                || 0u == binding.byteSize
                || byteEnd > layout.constantBufferByteSize
                || (!isPrefix && binding.byteOffset < kForwardNumericPrefixByteSize))
            {
                outError = "Forward custom numeric property reflection 범위 불일치: ";
                outError += binding.name;
                return false;
            }
            if (!isPrefix)
            {
                hasReflectedTail = true;
            }
        }
        if (layout.constantBufferByteSize > kForwardNumericPrefixByteSize
            && !hasReflectedTail)
        {
            outError = "Forward custom numeric tail이 ShaderMeta에 선언되지 않았다";
            return false;
        }
        return true;
    }

    bool ValidateForwardTextureLayout(const ShaderMetaBindingLayout& layout,
        std::string& outError)
    {
        std::array<bool, kForwardMaterialTextureSlotCount> occupied{};
        std::vector<std::string_view> names;
        for (const ShaderMetaPropertyBinding& binding : layout.properties)
        {
            if (ShaderPropertyType::Texture2D != binding.propertyType) continue;
            if (binding.name.empty()
                || RHIShaderResourceKind::Texture != binding.resourceKind
                || 0u != binding.registerSpace
                || binding.registerIndex < kForwardMaterialTextureFirstRegister
                || binding.registerIndex >= kForwardMaterialTextureFirstRegister
                    + kForwardMaterialTextureSlotCount)
            {
                outError = "Forward texture property가 t4..t7/space0 범위 밖이다: "
                    + binding.name;
                return false;
            }
            const std::size_t slot = binding.registerIndex
                - kForwardMaterialTextureFirstRegister;
            if (occupied[slot]
                || std::find(names.begin(), names.end(), binding.name) != names.end())
            {
                outError = "Forward texture property 이름/register가 중복이다: "
                    + binding.name;
                return false;
            }
            occupied[slot] = true;
            names.push_back(binding.name);
        }
        return true;
    }

    bool ValidateForwardTextureSnapshot(
        const EnhancedForwardMaterialDrawSnapshot& snapshot,
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
            outError = "Forward draw texture owner 수가 reflection schema와 다르다";
            return false;
        }

        std::array<bool, kForwardMaterialTextureSlotCount> occupied{};
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
                || texture.registerIndex < kForwardMaterialTextureFirstRegister
                || texture.registerIndex >= kForwardMaterialTextureFirstRegister
                    + kForwardMaterialTextureSlotCount)
            {
                outError = "Forward draw texture binding이 reflection register와 다르다: ";
                outError += texture.propertyName;
                return false;
            }
            const std::size_t slot = texture.registerIndex
                - kForwardMaterialTextureFirstRegister;
            if (occupied[slot]
                || std::find(names.begin(), names.end(), texture.propertyName) != names.end())
            {
                outError = "Forward draw texture 이름/register가 중복이다: "
                    + texture.propertyName;
                return false;
            }
            occupied[slot] = true;
            names.push_back(texture.propertyName);
        }
        return true;
    }

    // HLSL의 ShadeInstance와 크기·배치가 같아야 한다(구조화 버퍼 보폭).
    struct ShadeInstance
    {
        math::matrix4x4 world{};
        math::vector4   baseColor{};
        float          metallic{ 0.f };
        float          roughness{ 1.f };
        uint32_t       useNormalMap{ 0 };
        // bit0: material texture table, bit1: owning ShaderMeta snapshot.
        uint32_t       materialFlags{ 0 };
        // P2d-b Material::m_flowInfo + producer frame time. HLSL의 뒤 32B와
        // 정확히 같고, draw마다 독립적이라 인접 instancing에도 섞이지 않는다.
        math::vector4  flowWindVector{};
        math::vector2  flowUvScroll{};
        float          flowTotalSeconds{ 0.f };
        float          flowDeltaSeconds{ 0.f };
    };
    // ★ 보폭이 어긋나면 컴파일도 검증도 통과하고 GPU만 엉뚱한 필드를 읽는다.
    //   마지막 넷이 정확히 16바이트를 채우는 것이 핵심 — 그래야 구조화
    //   버퍼의 타이트 패킹과 16바이트 논스트래들 규칙이 같은 답을 낸다.
    //   필드를 더할 때 이 수를 함께 고칠 것.
    static_assert(sizeof(ShadeInstance) == 128,
        "HLSL ShadeInstance와 구조화 버퍼 보폭이 어긋났다");
    static_assert(offsetof(ShadeInstance, flowWindVector) == 96u);
    static_assert(offsetof(ShadeInstance, flowUvScroll) == 112u);
    static_assert(offsetof(ShadeInstance, flowTotalSeconds) == 120u);
    static_assert(offsetof(ShadeInstance, flowDeltaSeconds) == 124u);
    static_assert(std::is_trivially_copyable_v<ShadeInstance>);

    struct ShadeParams
    {
        math::matrix4x4 viewProjection{};
        uint32_t       tileGridX{ 0 };
        uint32_t       tileGridY{ 0 };
        uint32_t       lightCount{ 0 };
        uint32_t       pad0{ 0 };
        math::vector4   eyePosition{};
        uint32_t       hasIbl{ 0 };
        uint32_t       pad1[3]{};

        // 그림자. Deferred의 LightingConstants와 같은 값을 같은 뜻으로 싣는다.
        math::matrix4x4 lightViewProjection[kShadowCascadeCount]{};
        math::vector4   cameraForward{};
        math::vector4   cascadeSplits{};
        math::vector4   shadowBias{};
        uint32_t       hasShadow{ 0 };
        float          cascadeBlendBand{ 0.f };
        uint32_t       pad2[2]{};
    };
    // ShadeInstance와 같은 이유의 가드. cbuffer는 16바이트 경계·논스트래들
    // 규칙이라 뒤에 필드를 더할 때 이 수도 함께 고쳐야 한다.
    static_assert(sizeof(ShadeParams) == 368,
        "HLSL ShadeParams와 cbuffer 보폭이 어긋났다");
    static_assert(offsetof(ShadeParams, viewProjection) == 0u);
    static_assert(offsetof(ShadeParams, eyePosition) == 80u);
    static_assert(offsetof(ShadeParams, lightViewProjection) == 112u);
    static_assert(offsetof(ShadeParams, hasShadow) == 352u);
    static_assert(std::is_standard_layout_v<ShadeParams>);
    static_assert(std::is_trivially_copyable_v<ShadeParams>);

    struct CullParams
    {
        math::matrix4x4 view{};
        math::matrix4x4 inverseProjection{};
        uint32_t      screenWidth{ 0 };
        uint32_t      screenHeight{ 0 };
        uint32_t      tileGridX{ 0 };
        uint32_t      tileGridY{ 0 };
        uint32_t      lightCount{ 0 };
        uint32_t      pad[3]{};
    };

    static_assert(sizeof(CullParams) == 160u);
    static_assert(std::is_trivially_copyable_v<CullParams>);

    bool CompileFwdShaderEntry(const char* file,
        const RHIShaderPermutation& permutation,
        const char* entry, const char* target,
        RHIShaderBlob& outBlob, std::string& outError)
    {
        return RHIShaderCompiler::CompileFile(file, entry, target, permutation,
            outBlob, outError);
    }

    bool CompileFwdShader(const char* file,
        const RHIShaderPermutation& permutation,
        RHIShaderBlob& outBlob, std::string& outError)
    {
        return RHIShaderCompiler::CompileFile(file, "CSMain", "cs_5_0", permutation,
            outBlob, outError);
    }
}

bool EnhancedForwardPass::MaterialKey::operator==(const MaterialKey& other) const
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
        && snapshot->propertyBytes == other.snapshot->propertyBytes
        && snapshot->flow.Values() == other.snapshot->flow.Values();
}

bool EnhancedForwardPass::MaterialKey::operator<(const MaterialKey& other) const
{
    const ShaderMetaHandle leftHandle = snapshot
        ? snapshot->shaderMetaHandle : ShaderMetaHandle{};
    const ShaderMetaHandle rightHandle = other.snapshot
        ? other.snapshot->shaderMetaHandle : ShaderMetaHandle{};
    if (leftHandle.slot != rightHandle.slot) return leftHandle.slot < rightHandle.slot;
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
        if (snapshot->flow.Values() != other.snapshot->flow.Values())
            return snapshot->flow.Values() < other.snapshot->flow.Values();
    }
    return std::lexicographical_compare(textures.begin(), textures.end(),
        other.textures.begin(), other.textures.end(), std::less<Texture*>{});
}

bool EnhancedForwardPass::ResolveMaterialView(const EnhancedDrawItem& draw,
    MaterialView& outView, std::string& outError)
{
    if (draw.forwardMaterialSnapshot)
    {
        const EnhancedForwardMaterialDrawSnapshot& snapshot =
            *draw.forwardMaterialSnapshot;
        if (!snapshot.IsValid())
        {
            outError = "Forward material snapshot의 b2/ShaderMeta 계약이 invalid다";
            return false;
        }
        if (!ValidateForwardTextureSnapshot(snapshot, outError)) return false;
        for (const EnhancedMaterialTextureBinding& binding : snapshot.textureBindings)
        {
            const std::size_t slot = binding.registerIndex
                - kForwardMaterialTextureFirstRegister;
            outView.textures[slot] = binding.textureOwner.get();
        }
        outView.baseColorFactor = snapshot.baseColorFactor;
        outView.metallic = snapshot.metallic;
        outView.roughness = snapshot.roughness;
        outView.useNormalMap = snapshot.useNormalMap;
        outView.snapshot = draw.forwardMaterialSnapshot;
        return true;
    }

    outView.textures[0] = draw.baseColor;
    outView.textures[1] = draw.normalMap;
    outView.textures[2] = draw.occRoughMetal;
    outView.textures[3] = draw.emissive;
    outView.baseColorFactor = draw.baseColorFactor;
    outView.metallic = draw.metallic;
    outView.roughness = draw.roughness;
    outView.useNormalMap = draw.useNormalMap;
    return true;
}

EnhancedForwardPass::MaterialKey EnhancedForwardPass::MakeMaterialKey(
    const EnhancedDrawItem& draw) const
{
    MaterialKey key{};
    if (draw.forwardMaterialSnapshot && draw.forwardMaterialSnapshot->IsValid())
    {
        for (const EnhancedMaterialTextureBinding& binding :
            draw.forwardMaterialSnapshot->textureBindings)
        {
            if (binding.registerSpace != 0u
                || binding.registerIndex < kForwardMaterialTextureFirstRegister
                || binding.registerIndex >= kForwardMaterialTextureFirstRegister
                    + kForwardMaterialTextureSlotCount)
            {
                continue;
            }
            key.textures[binding.registerIndex
                - kForwardMaterialTextureFirstRegister] = binding.textureOwner.get();
        }
        key.snapshot = draw.forwardMaterialSnapshot;
    }
    else
    {
        key.textures = {
            draw.baseColor, draw.normalMap, draw.occRoughMetal, draw.emissive };
    }
    return key;
}

bool EnhancedForwardPass::ResolveShaderVariant(
    const EnhancedForwardMaterialDrawSnapshot& snapshot,
    RHIPipelineHandle& outShadePipeline,
    RHIPipelineHandle& outReferencePipeline,
    std::shared_ptr<const ShaderMetaBindingLayout>& outLayout) const
{
    if (snapshot.shaderMetaHandle == m_shaderMetaHandle
        && snapshot.permutationKey == m_defaultPermutationKey
        && snapshot.keywordSelections == m_defaultKeywordSelections)
    {
        outShadePipeline = m_shadePipelineRequest.GetHandle();
        outReferencePipeline = m_referencePipelineRequest.GetHandle();
        outLayout = m_shaderBindingLayout;
        return outShadePipeline.IsValid() && outReferencePipeline.IsValid()
            && nullptr != outLayout;
    }

    const ShaderVariantKey key{ snapshot.shaderMetaHandle,
        snapshot.permutationKey, snapshot.keywordSelections };
    const auto found = m_shaderVariants.find(key);
    if (found == m_shaderVariants.end()) return false;
    outShadePipeline = found->second.shade.GetHandle();
    outReferencePipeline = found->second.reference.GetHandle();
    outLayout = found->second.layout;
    return outShadePipeline.IsValid() && outReferencePipeline.IsValid()
        && nullptr != outLayout;
}

RHIPipelineHandle EnhancedForwardPass::GetShaderVariantPipeline(
    ShaderMetaHandle handle, RHIShaderPermutationKey permutationKey,
    std::span<const std::uint16_t> keywordSelections,
    bool referencePath) const
{
    if (handle == m_shaderMetaHandle && permutationKey == m_defaultPermutationKey
        && keywordSelections.size() == m_defaultKeywordSelections.size()
        && std::equal(keywordSelections.begin(), keywordSelections.end(),
            m_defaultKeywordSelections.begin()))
    {
        return referencePath ? GetReferencePSO() : GetShadePSO();
    }
    const ShaderVariantKey key{ handle, permutationKey,
        std::vector<std::uint16_t>(keywordSelections.begin(), keywordSelections.end()) };
    const auto found = m_shaderVariants.find(key);
    if (found == m_shaderVariants.end()) return {};
    return referencePath ? found->second.reference.GetHandle()
        : found->second.shade.GetHandle();
}

bool EnhancedForwardPass::Initialize(const EnhancedFrameContext& context, std::string& outError)
{
    if (nullptr == context.resources || nullptr == context.psoManager ||
        nullptr == context.rootSignatures)
    {
        outError = "Forward+ 패스 컨텍스트가 불완전하다";
        return false;
    }

    return CreatePipelines(context, outError);
}

bool EnhancedForwardPass::CreatePipelines(const EnhancedFrameContext& context, std::string& outError)
{
    // 컬링 루트 시그니처: b0 상수 · t0 깊이(테이블) · t1 광원(루트 SRV)
    // · u0/u1 타일 버퍼(테이블).
    const RHIPipelineLayoutParam params[] = {
        RHILayout::Cbv(0),
        RHILayout::SrvTable(1, 0),
        RHILayout::Srv(1),   // 광원 배열 — 업로드 링 조각의 GPU 주소를 그대로 꽂는다
        RHILayout::UavBufferTable(2, 0),
    };

    RHIPipelineLayoutDesc rootDesc{};
    rootDesc.params = params;

    const auto root = context.rootSignatures->GetOrCreate(rootDesc, outError);
    if (!root.IsValid()) return false;

    const std::string tileSize = std::to_string(kTileSize);
    const std::string maxLights = std::to_string(kMaxLightsPerTile);
    RHIShaderPermutation forwardPermutation;
    if (!forwardPermutation.Set("TILE_SIZE", tileSize, outError)
        || !forwardPermutation.Set("MAX_LIGHTS_PER_TILE", maxLights, outError))
        return false;

    RHIShaderBlob blob;
    if (!CompileFwdShader(kCullShaderFile, forwardPermutation, blob, outError)) return false;

    RHIComputePipelineDesc desc{};
    desc.csBytecode = blob.Data();
    desc.csSize = blob.Size();
    desc.layout = root;

    m_cullPSO = context.psoManager->GetOrCreateCompute(desc, outError);
    if (!m_cullPSO.IsValid()) return false;

    // ── 셰이딩 루트 시그니처 ──
    //
    // b0 상수 · t0 인스턴스 · t1 광원 · t2 타일 카운트 · t3 타일 목록.
    // 넷 다 구조화 버퍼라 루트 SRV로 꽂는다 — 디스크립터 테이블을 만들 이유가
    // 없고, 드로우마다 인스턴스 버퍼 주소만 바뀌므로 이쪽이 싸다.
    //
    // t4(재질 텍스처)와 s0(샘플러)만 테이블이다. 텍스처는 루트 SRV로 못
    // 꽂는다 — 루트 SRV는 버퍼 전용이고 Texture2D는 디스크립터를 요구한다.
    // t8~t11 은 프레임 내내 같은 텍스처들 — IBL 셋과 그림자 맵. 드로우마다
    // 바뀌는 재질과 달리 한 번만 걸면 되므로 한 테이블에 묶는다. 차원이
    // 섞여 있지만(TextureCube · Texture2D · Texture2DArray) SRV 범위는
    // 그것을 가리지 않는다 — Deferred도 아홉을 한 범위로 묶는다.
    const RHIPipelineLayoutParam shadeParams[] = {
        RHILayout::Cbv(0),
        RHILayout::Srv(0),
        RHILayout::Srv(1),
        RHILayout::Srv(2),
        RHILayout::Srv(3),
        RHILayout::SrvTable(4, 4, RHIShaderVisibility::Pixel),   // t4~t7 baseColor · normal · ORM · emissive
        RHILayout::SrvTable(4, 8, RHIShaderVisibility::Pixel),   // t8~t11 조도 · 프리필터 · BRDF LUT · 그림자
        RHILayout::SamplerTable(3, 0, RHIShaderVisibility::Pixel),  // 재질 · IBL · 그림자 비교
        RHILayout::Cbv(2, RHIShaderVisibility::Pixel),           // b2 — M6 Forward material property block
    };

    RHIPipelineLayoutDesc shadeRootDesc{};
    shadeRootDesc.params = shadeParams;
    shadeRootDesc.allowInputAssembler = true;

    const auto shadeRoot = context.rootSignatures->GetOrCreate(shadeRootDesc, outError);
    if (!shadeRoot.IsValid()) return false;

    // 샘플러 둘을 연속으로 만든다 — 테이블은 연속이어야 하므로 따로 만들어
    // 인접을 기대하면 안 된다(Deferred와 같은 이유).
    {
        const RHISamplerDesc samplers[] = {
            // 재질용. GBuffer와 같은 설정(선형 · WRAP)이라 같은 UV가 같은 텍셀을
            // 집는다 — 다르면 불투명과 투명이 같은 재질에서 갈린다.
            RHISampler::Linear(RHIAddressMode::Wrap),

            // IBL용 선형 클램프. 거칠기가 프리필터의 밉 좌표라 밉 사이 보간이
            // 필요하다 — 포인트로 읽으면 거칠기 단차가 띠로 보인다.
            RHISampler::Linear(RHIAddressMode::Clamp),

            // 그림자 비교 샘플러. Deferred와 같은 설정이라야 같은 자리에서 두
            // 경로가 같은 그늘을 낸다 — 경계 색 흰색은 '맵 밖은 빛을 받음'이다.
            RHISampler::Comparison(RHICompareOp::LessEqual, RHIAddressMode::Border,
                RHIBorderColor::OpaqueWhite),
        };

        m_sampler = context.resources->CreateSamplers(samplers);
        if (!m_sampler.IsValid())
        {
            outError = "Forward+ 샘플러 생성 실패";
            return false;
        }
    }

    static const RHIInputElement kInputElements[] = {
        { "POSITION", 0, RHIFormat::RGB32Float, 0,  0, 0 },
        { "NORMAL",   0, RHIFormat::RGB32Float, 0, 12, 0 },
        { "TEXCOORD", 0, RHIFormat::RG32Float,    0, 24, 0 },
        { "TANGENT",  0, RHIFormat::RGB32Float, 0, 40, 0 },
        { "BINORMAL", 0, RHIFormat::RGB32Float, 0, 52, 0 },
    };

    // GBuffer와 같은 정점 레이아웃을 쓰므로 같은 단정을 건다.
    static_assert(offsetof(Vertex, normal) == 12, "Vertex 레이아웃이 바뀌었다");
    static_assert(offsetof(Vertex, uv0) == 24, "Vertex 레이아웃이 바뀌었다");
    static_assert(offsetof(Vertex, tangent) == 40, "Vertex 레이아웃이 바뀌었다");
    static_assert(offsetof(Vertex, bitangent) == 52, "Vertex 레이아웃이 바뀌었다");

    // 컬링 경로와 참조 경로를 둘 다 만든다. 참조는 대조의 정답지다.
    struct ShadeVariant
    {
        bool                  reference;
        RHIGraphicsPipelineRequest* target;
    };
    const ShadeVariant variants[] = {
        { false, &m_shadePipelineRequest },
        { true,  &m_referencePipelineRequest },
    };

    for (const ShadeVariant& variant : variants)
    {
        RHIShaderPermutation shadePermutation = forwardPermutation;
        if (variant.reference
            && !shadePermutation.Enable("REFERENCE_PATH", outError))
            return false;

        RHIShaderBlob vsBlob;
        RHIShaderBlob psBlob;
        if (!CompileFwdShaderEntry(kShadeShaderFile, shadePermutation,
                "VSMain", "vs_5_0", vsBlob, outError)) return false;
        if (!CompileFwdShaderEntry(kShadeShaderFile, shadePermutation,
                "PSMain", "ps_5_0", psBlob, outError)) return false;

        RHIGraphicsPipelineDesc shadeDesc{};
        shadeDesc.inputElements = kInputElements;
        shadeDesc.inputElementCount = _countof(kInputElements);
        shadeDesc.vsBytecode = vsBlob.Data();
        shadeDesc.vsSize = vsBlob.Size();
        shadeDesc.psBytecode = psBlob.Data();
        shadeDesc.psSize = psBlob.Size();
        shadeDesc.layout = shadeRoot;
        // 깊이 테스트를 켠다. 포워드 물체는 이미 그려진 불투명 기하 뒤에
        // 있으면 가려져야 한다 — 끄면 벽 뒤 물체가 비쳐 보이는데, 그것은
        // 화면을 봐야만 알 수 있고 수치 검증에는 안 잡히는 부류다.
        shadeDesc.depthEnable = true;
        // ★ 깊이는 보되 쓰지 않는다. 투명이 깊이를 쓰면 뒤에 있는 다른
        //   투명면이 그 깊이에 가려져 사라진다 — 앞의 유리가 뒤의 유리를
        //   지워 버리는 그 증상이다. 테스트(LESS)는 유지해 불투명 기하가
        //   투명을 가리는 것은 그대로 둔다.
        shadeDesc.depthWriteMask = RHIDepthWrite::Zero;
        // 알파 블렌딩(SRC_ALPHA/INV_SRC_ALPHA). 이것이 없으면 알파를 내도
        // 덮어쓰기라 투명이 성립하지 않는다.
        shadeDesc.blendEnable = true;
        shadeDesc.dsvFormat = kDepthFormat;
        // ★ 뒷면을 자른다.
        //
        //   GBuffer는 CULL_MODE_NONE인데도 멀쩡하다 — 깊이를 쓰기 때문에
        //   앞면이 깊이 테스트로 뒷면을 막는다. 투명은 깊이를 쓰지 않으므로
        //   (위의 depthWriteMask=ZERO) 그 보호가 없다: 닫힌 물체의 뒷면이
        //   앞면 위에 덧그려져 법선이 반대인 어두운 얼룩이 생긴다. 구에서
        //   먼저 눈에 띄었다.
        //
        //   양면 투명(잎사귀·천)은 뒷면→앞면 2패스가 정석이지만, 그건
        //   재질이 '양면인가'를 알려 줘야 성립한다. 지금 재질에 그 표시가
        //   없으므로 흔한 기본값(닫힌 물체)을 택한다.
        shadeDesc.cullMode = RHICullMode::Back;
        shadeDesc.numRenderTargets = 1;
        shadeDesc.rtvFormats[0] = kOutputFormat;

        if (!variant.target->Create(*context.psoManager, shadeDesc, outError))
            return false;
    }

    return true;
}

bool EnhancedForwardPass::BuildShadePipelineDesc(
    const EnhancedFrameContext& context, const char* shaderFile,
    const char* vertexEntry, const char* pixelEntry,
    const ShaderRenderState* renderState,
    const RHIShaderPermutation& permutation,
    RHIGraphicsPipelineDesc& outDesc, RHIShaderBlob& outVs,
    RHIShaderBlob& outPs, std::string& outError)
{
    if (!CompileFwdShaderEntry(shaderFile, permutation, vertexEntry, "vs_5_0",
            outVs, outError)
        || !CompileFwdShaderEntry(shaderFile, permutation, pixelEntry, "ps_5_0",
            outPs, outError))
    {
        return false;
    }

    const RHIPipelineLayoutParam params[] = {
        RHILayout::Cbv(0),
        RHILayout::Srv(0),
        RHILayout::Srv(1),
        RHILayout::Srv(2),
        RHILayout::Srv(3),
        RHILayout::SrvTable(4, 4, RHIShaderVisibility::Pixel),
        RHILayout::SrvTable(4, 8, RHIShaderVisibility::Pixel),
        RHILayout::SamplerTable(3, 0, RHIShaderVisibility::Pixel),
        RHILayout::Cbv(2, RHIShaderVisibility::Pixel),
    };
    RHIPipelineLayoutDesc rootDesc{};
    rootDesc.params = params;
    rootDesc.allowInputAssembler = true;
    const auto root = context.rootSignatures->GetOrCreate(rootDesc, outError);
    if (!root.IsValid()) return false;

    static const RHIInputElement kInputElements[] = {
        { "POSITION", 0, RHIFormat::RGB32Float, 0,  0, 0 },
        { "NORMAL",   0, RHIFormat::RGB32Float, 0, 12, 0 },
        { "TEXCOORD", 0, RHIFormat::RG32Float,  0, 24, 0 },
        { "TANGENT",  0, RHIFormat::RGB32Float, 0, 40, 0 },
        { "BINORMAL", 0, RHIFormat::RGB32Float, 0, 52, 0 },
    };

    outDesc = {};
    outDesc.inputElements = kInputElements;
    outDesc.inputElementCount = _countof(kInputElements);
    outDesc.vsBytecode = outVs.Data();
    outDesc.vsSize = outVs.Size();
    outDesc.psBytecode = outPs.Data();
    outDesc.psSize = outPs.Size();
    outDesc.layout = root;
    outDesc.depthEnable = true;
    outDesc.depthWriteMask = RHIDepthWrite::Zero;
    outDesc.blendEnable = true;
    outDesc.cullMode = RHICullMode::Back;
    outDesc.numRenderTargets = 1;
    outDesc.rtvFormats[0] = kOutputFormat;
    outDesc.dsvFormat = kDepthFormat;
    if (nullptr != renderState) renderState->ApplyTo(outDesc);
    return true;
}

bool EnhancedForwardPass::BuildShaderMetaPipelineDesc(
    const EnhancedFrameContext& context, const ShaderMeta& meta,
    std::span<const std::uint16_t> keywordSelections,
    bool referencePath, RHIGraphicsPipelineDesc& outDesc,
    RHIShaderBlob& outVs, RHIShaderBlob& outPs,
    RHIShaderPermutationKey& outPermutationKey,
    std::shared_ptr<const ShaderMetaBindingLayout>& outLayout,
    std::string& outError)
{
    const auto passIt = std::find_if(meta.passes.begin(), meta.passes.end(),
        [](const ShaderPassDesc& pass) { return pass.name == "Forward"; });
    if (passIt == meta.passes.end())
    {
        outError = "Forward ShaderMeta에 Forward pass가 없다";
        return false;
    }
    const ShaderPassDesc& pass = *passIt;
    if (pass.IsCompute() || !pass.vertex || !pass.pixel
        || ShaderPassQueue::Transparent != pass.queue)
    {
        outError = "Forward ShaderMeta pass는 transparent VS+PS graphics여야 한다";
        return false;
    }

    const std::uint32_t passIndex = static_cast<std::uint32_t>(
        std::distance(meta.passes.begin(), passIt));
    ShaderMetaPermutation materialPermutation;
    if (!ShaderPermutationDomain::Resolve(meta, passIndex, keywordSelections,
            materialPermutation, outError))
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
            outError = "Forward ShaderMeta source가 shader root 밖이다";
            return false;
        }
    }
    const std::string shaderFile = shaderPath.generic_string();
    if (shaderFile.empty() || pass.vertex->entry.empty() || pass.pixel->entry.empty())
    {
        outError = "Forward ShaderMeta source/entry가 비었다";
        return false;
    }

    RHIShaderPermutation compilePermutation = materialPermutation.defines;
    if (!compilePermutation.Set("TILE_SIZE", std::to_string(kTileSize), outError)
        || !compilePermutation.Set("MAX_LIGHTS_PER_TILE",
            std::to_string(kMaxLightsPerTile), outError)
        || (referencePath
            && !compilePermutation.Enable("REFERENCE_PATH", outError)))
    {
        return false;
    }

    if (!BuildShadePipelineDesc(context, shaderFile.c_str(),
            pass.vertex->entry.c_str(), pass.pixel->entry.c_str(), &pass.state,
            compilePermutation, outDesc, outVs, outPs, outError))
    {
        return false;
    }

    std::vector<RHIShaderReflection> reflections(2);
    if (!RHIShaderCompiler::ReflectFile(shaderFile, pass.vertex->entry, "vs_5_0",
            RHIShaderCompiler::GetOutput(), compilePermutation,
            reflections[0], outError)
        || !RHIShaderCompiler::ReflectFile(shaderFile, pass.pixel->entry, "ps_5_0",
            RHIShaderCompiler::GetOutput(), compilePermutation,
            reflections[1], outError))
    {
        return false;
    }

    ShaderMetaBindingLayout layout;
    if (!ShaderMetaReflection::Resolve(meta, reflections, layout, outError))
        return false;
    if (!ValidateForwardNumericLayout(layout, outError)) return false;
    if (!ValidateForwardTextureLayout(layout, outError)) return false;

    outPermutationKey = materialPermutation.key;
    outLayout = std::make_shared<ShaderMetaBindingLayout>(std::move(layout));
    return true;
}

bool EnhancedForwardPass::ApplyShaderMeta(const EnhancedFrameContext& context,
    ShaderMetaHandle handle, const ShaderMeta& meta,
    RHICompletionPoint retireAfter, std::string& outError)
{
    if (!handle.IsValid())
    {
        outError = "Forward ShaderMeta generation handle이 비었다";
        return false;
    }
    if (handle == m_shaderMetaHandle) return true;

    const std::vector<std::uint16_t> defaultSelections(meta.keywords.size(), 0);
    RHIGraphicsPipelineDesc shadeDesc{};
    RHIGraphicsPipelineDesc referenceDesc{};
    RHIShaderBlob shadeVs, shadePs, referenceVs, referencePs;
    RHIShaderPermutationKey shadeKey{}, referenceKey{};
    std::shared_ptr<const ShaderMetaBindingLayout> shadeLayout, referenceLayout;
    if (!BuildShaderMetaPipelineDesc(context, meta, defaultSelections, false,
            shadeDesc, shadeVs, shadePs, shadeKey, shadeLayout, outError)
        || !BuildShaderMetaPipelineDesc(context, meta, defaultSelections, true,
            referenceDesc, referenceVs, referencePs, referenceKey,
            referenceLayout, outError))
    {
        return false;
    }
    if (shadeKey != referenceKey || !shadeLayout || !referenceLayout
        || *shadeLayout != *referenceLayout || shadeDesc.layout != referenceDesc.layout)
    {
        outError = "Forward 일반/Reference 후보의 permutation/layout pair가 다르다";
        return false;
    }

    ShaderVariant candidate;
    candidate.layout = shadeLayout;
    if (!candidate.shade.Create(*context.psoManager, shadeDesc, outError))
    {
        return false;
    }
    if (!candidate.reference.Create(*context.psoManager, referenceDesc, outError))
    {
        // shade handle은 다른 request/owner도 공유할 수 있는 cache entry다.
        // 여기서 invalidate하지 않고 local request만 버리면 retry가 같은 entry를
        // 재사용하며, 외부 holder도 stale되지 않는다.
        return false;
    }

    std::vector<RHIPipelineHandle> removedPipelines{
        m_shadePipelineRequest.GetHandle(), m_referencePipelineRequest.GetHandle() };
    const std::uint32_t previousSlot = m_shaderMetaHandle.slot;
    for (auto it = m_shaderVariants.begin(); it != m_shaderVariants.end();)
    {
        if (0u != previousSlot && it->first.meta.slot == previousSlot)
        {
            removedPipelines.push_back(it->second.shade.GetHandle());
            removedPipelines.push_back(it->second.reference.GetHandle());
            it = m_shaderVariants.erase(it);
        }
        else
        {
            ++it;
        }
    }

    // 두 candidate request를 모두 만든 뒤 한 경계에서 게시한다.
    m_shadePipelineRequest = std::move(candidate.shade);
    m_referencePipelineRequest = std::move(candidate.reference);
    m_shaderMetaHandle = handle;
    m_defaultPermutationKey = shadeKey;
    m_defaultKeywordSelections = defaultSelections;
    m_shaderBindingLayout = std::move(candidate.layout);

    const auto stillHeld = [this](RHIPipelineHandle pipeline)
    {
        if (pipeline == m_shadePipelineRequest.GetHandle()
            || pipeline == m_referencePipelineRequest.GetHandle()) return true;
        return std::any_of(m_shaderVariants.begin(), m_shaderVariants.end(),
            [pipeline](const auto& entry)
            {
                return pipeline == entry.second.shade.GetHandle()
                    || pipeline == entry.second.reference.GetHandle();
            });
    };
    std::vector<RHIPipelineHandle> invalidated;
    for (const RHIPipelineHandle pipeline : removedPipelines)
    {
        if (!pipeline.IsValid() || stillHeld(pipeline)
            || std::find(invalidated.begin(), invalidated.end(), pipeline)
                != invalidated.end())
        {
            continue;
        }
        context.psoManager->InvalidatePipeline(pipeline, retireAfter);
        invalidated.push_back(pipeline);
    }
    return true;
}

bool EnhancedForwardPass::EnsureShaderMetaVariant(
    const EnhancedFrameContext& context, ShaderMetaHandle handle,
    const ShaderMeta& meta,
    std::span<const std::uint16_t> keywordSelections,
    RHIShaderPermutationKey& outPermutationKey,
    std::shared_ptr<const ShaderMetaBindingLayout>& outLayout,
    std::string& outError)
{
    if (!handle.IsValid())
    {
        outError = "Forward material variant의 ShaderMeta generation이 비었다";
        return false;
    }
    const auto passIt = std::find_if(meta.passes.begin(), meta.passes.end(),
        [](const ShaderPassDesc& pass) { return pass.name == "Forward"; });
    if (passIt == meta.passes.end())
    {
        outError = "Forward material variant에 Forward pass가 없다";
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
    const std::vector<std::uint16_t> selectionCopy(
        keywordSelections.begin(), keywordSelections.end());
    if (handle == m_shaderMetaHandle && resolved.key == m_defaultPermutationKey
        && selectionCopy == m_defaultKeywordSelections)
    {
        outLayout = m_shaderBindingLayout;
        return m_shadePipelineRequest.IsValid()
            && m_referencePipelineRequest.IsValid() && nullptr != outLayout;
    }

    const ShaderVariantKey key{ handle, resolved.key, selectionCopy };
    const auto existing = m_shaderVariants.find(key);
    if (existing != m_shaderVariants.end())
    {
        outLayout = existing->second.layout;
        return existing->second.shade.IsValid()
            && existing->second.reference.IsValid() && nullptr != outLayout;
    }

    RHIGraphicsPipelineDesc shadeDesc{}, referenceDesc{};
    RHIShaderBlob shadeVs, shadePs, referenceVs, referencePs;
    RHIShaderPermutationKey shadeKey{}, referenceKey{};
    std::shared_ptr<const ShaderMetaBindingLayout> shadeLayout, referenceLayout;
    if (!BuildShaderMetaPipelineDesc(context, meta, keywordSelections, false,
            shadeDesc, shadeVs, shadePs, shadeKey, shadeLayout, outError)
        || !BuildShaderMetaPipelineDesc(context, meta, keywordSelections, true,
            referenceDesc, referenceVs, referencePs, referenceKey,
            referenceLayout, outError))
    {
        return false;
    }
    if (shadeKey != resolved.key || referenceKey != resolved.key
        || !shadeLayout || !referenceLayout || *shadeLayout != *referenceLayout
        || shadeDesc.layout != referenceDesc.layout)
    {
        outError = "Forward material variant의 일반/Reference identity가 불완전하다";
        return false;
    }
    if (!m_shadePipelineRequest.IsValid()
        || shadeDesc.layout != m_shadePipelineRequest.GetDesc().layout)
    {
        outError = "Forward material ShaderMeta pipeline layout이 primary pass와 다르다";
        return false;
    }

    ShaderVariant candidate;
    candidate.layout = shadeLayout;
    if (!candidate.shade.Create(*context.psoManager, shadeDesc, outError))
    {
        return false;
    }
    if (!candidate.reference.Create(*context.psoManager, referenceDesc, outError))
    {
        // 첫 candidate는 cache에 재사용 가능한 채로 남긴다. 전역 handle을
        // invalidate하면 같은 desc를 공유한 다른 owner까지 끊어진다.
        return false;
    }
    auto [inserted, accepted] =
        m_shaderVariants.try_emplace(key, std::move(candidate));
    if (!accepted)
    {
        outError = "Forward material variant cache insert가 충돌했다";
        return false;
    }
    outPermutationKey = shadeKey;
    outLayout = inserted->second.layout;
    outError.clear();
    return true;
}

std::uint32_t EnhancedForwardPass::CommitShaderMetaFrame(
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
            removedPipelines.push_back(it->second.shade.GetHandle());
            removedPipelines.push_back(it->second.reference.GetHandle());
            it = m_shaderVariants.erase(it);
            ++removedKeys;
        }
        else
        {
            ++it;
        }
    }

    const auto stillHeld = [this](RHIPipelineHandle pipeline)
    {
        if (pipeline == m_shadePipelineRequest.GetHandle()
            || pipeline == m_referencePipelineRequest.GetHandle()) return true;
        return std::any_of(m_shaderVariants.begin(), m_shaderVariants.end(),
            [pipeline](const auto& entry)
            {
                return pipeline == entry.second.shade.GetHandle()
                    || pipeline == entry.second.reference.GetHandle();
            });
    };
    std::vector<RHIPipelineHandle> invalidated;
    for (const RHIPipelineHandle pipeline : removedPipelines)
    {
        if (!pipeline.IsValid() || stillHeld(pipeline)
            || std::find(invalidated.begin(), invalidated.end(), pipeline)
                != invalidated.end())
        {
            continue;
        }
        context.psoManager->InvalidatePipeline(pipeline, retireAfter);
        invalidated.push_back(pipeline);
    }
    return removedKeys;
}

bool EnhancedForwardPass::EnsureTileBuffers(const EnhancedFrameContext& context,
    std::string& outError)
{
    const uint32_t tileTotal = m_tileCountX * m_tileCountY;
    if (0 == tileTotal) return true;

    // 크기가 그대로면 다시 만들지 않는다(SSGI 히스토리와 같은 계약).
    if (m_tileCountBuffer.IsValid())
    {
        if (m_allocatedTiles >= tileTotal) return true;
    }

    RHIBufferDesc desc{};
    desc.allowUnorderedAccess = true;

    // 두 배다. 앞 절반은 셰이딩이 읽는 자른 값, 뒤 절반은 자르기 전의
    // 원래 값 — 넘침이 얼마나 났는지가 수로 남아야 성능 수치를 믿을 수 있다.
    //
    // 초기 상태는 기본값(COMMON)을 쓴다. 버퍼는 첫 사용에서 승격되고,
    // UAV를 초기 상태로 주면 검증 레이어가 '무시한다'고 경고만 남긴다.
    desc.bytes = static_cast<uint64_t>(tileTotal) * 2ull * sizeof(uint32_t);
    desc.debugName = L"Forward+.TileCount";
    if (!context.resources->CreateBuffer(desc, m_tileCountBuffer, outError))
    {
        outError = "타일 카운트 버퍼 — " + outError;
        return false;
    }

    desc.bytes = static_cast<uint64_t>(tileTotal) * kMaxLightsPerTile * sizeof(uint32_t);
    desc.debugName = L"Forward+.TileList";
    if (!context.resources->CreateBuffer(desc, m_tileListBuffer, outError))
    {
        outError = "타일 목록 버퍼 — " + outError;
        return false;
    }

    m_allocatedTiles = tileTotal;

    // 새 리소스는 COMMON이다. 상태 멤버가 이전 버퍼의 끝 상태를 들고 있으면
    // 다음 Import의 첫 배리어가 틀린 before로 나간다 — 고정 해상도 자가
    // 검증으로는 안 잡히고 리사이즈에서만 나는 부류라 여기서 막는다.
    m_tileCountState = RHIResourceState::Common;
    m_tileListState = RHIResourceState::Common;
    return true;
}

bool EnhancedForwardPass::PrepareFrame(const EnhancedFrameContext& context, std::string& outError)
{
    // 타일 수. 화면이 타일 크기로 나누어떨어지지 않으면 가장자리 타일이
    // 화면 밖까지 걸치는데, 컬링 셰이더가 화면 경계로 자른다.
    m_tileCountX = (context.width + kTileSize - 1) / kTileSize;
    m_tileCountY = (context.height + kTileSize - 1) / kTileSize;
    m_lastDrawCount = 0;
    m_lastMaterialCount = 0;
    m_lastBatchCount = 0;
    m_batches.clear();

    if (nullptr != context.forwardDraws)
    {
        for (const EnhancedDrawItem& draw : *context.forwardDraws)
        {
            MaterialView material{};
            if (!ResolveMaterialView(draw, material, outError)) return false;
            if (material.snapshot)
            {
                RHIPipelineHandle shadePipeline{}, referencePipeline{};
                std::shared_ptr<const ShaderMetaBindingLayout> preparedLayout;
                if (!ResolveShaderVariant(*material.snapshot, shadePipeline,
                        referencePipeline, preparedLayout)
                    || !preparedLayout
                    || material.snapshot->bindingLayout != *preparedLayout)
                {
                    outError = "Forward draw material permutation/layout이 준비된 variant와 다르다";
                    return false;
                }
            }
            if (nullptr != draw.mesh) ++m_lastDrawCount;
        }
    }

    m_drawGeometry.clear();
    if (nullptr != context.meshCache && nullptr != context.forwardDraws)
    {
        for (const EnhancedDrawItem& draw : *context.forwardDraws)
        {
            if (nullptr == draw.mesh || m_drawGeometry.contains(draw.mesh)) continue;

            std::string uploadError;
            const RHIMeshBinding entry =
                context.meshCache->GetOrUpload(draw.mesh, uploadError);
            if (!entry.IsValid())
            {
                if (!uploadError.empty()) outError = uploadError;
                continue;
            }
            // I5-D34a: Forward의 레이아웃 축 전환은 D34c 몫이다. 그 전에
            // experiment packed 버퍼(마스크 != 0)를 96B 레이아웃 PSO로 그리면
            // TANGENT@40부터 어긋나 조용히 틀린다 — 안 그리고 알리는 쪽이
            // 낫다(fail-closed). FT 실측으로 포워드 드로우는 0이라 제품 씬
            // 파급도 0이다.
            if (0 != entry.vertexAttributeMask)
            {
                outError = "Forward는 experiment 정점 레이아웃을 아직 못 그린다"
                    "(D34c 전): " + draw.mesh->GetName();
                continue;
            }
            m_drawGeometry.emplace(draw.mesh, entry);
        }
    }

    // ── 재질 텍스처 운반 ──
    //
    // 기록 중에 올리지 않는다(GBuffer와 같은 규약). 캐시가 없는 자가 검증
    // 경로에서는 통째로 건너뛰고, 그때는 셰이더가 계수만으로 그린다.
    m_materialTextures.clear();
    if (nullptr != context.textureCache && nullptr != context.forwardDraws)
    {
        for (const EnhancedDrawItem& draw : *context.forwardDraws)
        {
            MaterialView material{};
            if (!ResolveMaterialView(draw, material, outError)) return false;
            const MaterialKey key = MakeMaterialKey(draw);
            if (m_materialTextures.find(key) != m_materialTextures.end()) continue;

            // 슬롯 의미를 따르는 폴백. 흰색 하나로 전부 때우면 뜻이 뒤집힌다 —
            // emissive에 흰색이면 텍스처 없는 재질이 전부 자체발광이고,
            // ORM에 흰색이면 B(금속)가 1이라 확산이 통째로 죽는다.
            // GBuffer와 같은 규칙이라야 같은 재질이 두 경로에서 같아 보인다.
            MaterialTextures textures{};
            bool anyValid = false;
            for (uint32_t i = 0; i < 4; ++i)
            {
                // ★ 실패를 IsValid로 거르려 하면 안 된다. GetOrUpload는 복구
                //   가능한 실패(2D 아님·멀티샘플·업로드 실패)에서 폴백을
                //   돌려주므로 IsValid는 늘 참이고, 그 자리에서 건너뛰면
                //   textureError가 통째로 사라진다 — 텍스처가 안 나오는데
                //   로그도 없는 상태가 된다. GBuffer처럼 무조건 전달한다.
                std::string textureError;
                RHITextureEntry uploaded{};
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
                if (!textureError.empty()) outError = textureError;

                textures.resources[i] = uploaded.handle;
            textures.formats[i] = uploaded.format;
                textures.mipLevels[i] = uploaded.mipLevels;
                anyValid = anyValid || uploaded.IsValid();
            }

            if (anyValid) m_materialTextures.emplace(key, textures);
        }
    }

    m_lastMaterialCount = static_cast<uint32_t>(m_materialTextures.size());
    BuildAdjacentBatches(context);
    return EnsureTileBuffers(context, outError);
}

void EnhancedForwardPass::BuildAdjacentBatches(const EnhancedFrameContext& context)
{
    m_batches.clear();
    m_lastBatchCount = 0;
    if (nullptr == context.forwardDraws) return;

    // 입력은 CaptureFromView가 이미 back-to-front로 정렬했다. 여기서는 절대
    // 재정렬하지 않고 바로 앞 draw와 완전히 호환될 때만 병합한다.
    for (std::size_t drawIndex = 0; drawIndex < context.forwardDraws->size(); ++drawIndex)
    {
        const EnhancedDrawItem& draw = (*context.forwardDraws)[drawIndex];
        if (nullptr == draw.mesh) continue;
        const auto geometry = m_drawGeometry.find(draw.mesh);
        if (geometry == m_drawGeometry.end() || !geometry->second.IsValid()) continue;

        const MaterialKey key = MakeMaterialKey(draw);
        RHIPipelineHandle shadePipeline = m_shadePipelineRequest.GetHandle();
        RHIPipelineHandle referencePipeline = m_referencePipelineRequest.GetHandle();
        if (key.snapshot)
        {
            std::shared_ptr<const ShaderMetaBindingLayout> ignoredLayout;
            if (!ResolveShaderVariant(*key.snapshot, shadePipeline,
                    referencePipeline, ignoredLayout))
            {
                continue;
            }
        }

        const bool adjacentCompatible = !m_batches.empty()
            && drawIndex == m_batches.back().firstDraw + m_batches.back().drawCount
            && m_batches.back().mesh == draw.mesh
            && m_batches.back().material == key
            && m_batches.back().shadePipeline == shadePipeline
            && m_batches.back().referencePipeline == referencePipeline;
        if (adjacentCompatible)
        {
            ++m_batches.back().drawCount;
            continue;
        }

        DrawBatch batch{};
        batch.firstDraw = drawIndex;
        batch.drawCount = 1;
        batch.mesh = draw.mesh;
        batch.material = key;
        batch.shadePipeline = shadePipeline;
        batch.referencePipeline = referencePipeline;
        m_batches.push_back(std::move(batch));
    }
    m_lastBatchCount = static_cast<uint32_t>(m_batches.size());
}

void EnhancedForwardPass::Declare(EnhancedRenderGraph& graph, const EnhancedFrameContext& context)
{
    m_output = RGHandle{};

    if (!m_inputs.depth.IsValid() || !m_cullPSO.IsValid() ||
        !m_tileCountBuffer.IsValid() || nullptr == context.lights)
    {
        return;
    }

    // ── 타일 버퍼를 그래프에 들인다 (R4-2b) ──
    //
    // ★ 예전에는 "그래프는 텍스처만 다루므로 타일 버퍼는 패스가 소유하고
    //   상태 전이도 패스가 책임진다"고 적혀 있었다. 재 보니 앞절이 틀렸다 —
    //   ImportTexture는 RGTextureDesc를 아예 안 건드린다. 외부 포인터와 현재
    //   상태만 담고, imported 리소스는 transient 생성도 풀 반납도 건너뛰며,
    //   배리어는 ID3D12Resource*에 거는 전이라 버퍼와 텍스처가 같다.
    //   버퍼 지원이 없던 것이 아니라 안 쓰고 있던 것이다.
    //
    //   들이고 나면 컬링(UAV) → 셰이딩(SRV) → 리드백(CopySource) 전이를
    //   전부 그래프가 만든다. 패스가 손으로 걸던 전이와 UAV 배리어가
    //   함께 사라지고, 끝 상태는 writeback이 멤버에 적어 다음 프레임의
    //   Import가 맞는 before로 시작한다.
    //
    // ★ 핸들째 넘긴다 (5c-3). 예전에는 `Resolve(handle)` 로 포인터를 풀어
    //   넘겼고, 그 두 줄이 **패스가 인터페이스 경유로 DX12 를 만지던 마지막
    //   자리**였다. 버퍼를 버퍼로 추적하는 모델은 아직 DX12 만 답할 수 있어
    //   그래프 안쪽에 있다(G-2b).
    m_tileCountHandle = graph.ImportBuffer(m_tileCountBuffer,
        m_tileCountState, "Forward+.TileCount", &m_tileCountState);
    m_tileListHandle = graph.ImportBuffer(m_tileListBuffer,
        m_tileListState, "Forward+.TileList", &m_tileListState);

    // ── 광원 컬링 ──
    graph.AddPass("Forward+.Cull",
        { { m_inputs.depth,    RHIResourceState::ShaderResource },
          { m_tileCountHandle, RHIResourceState::UnorderedAccess },
          { m_tileListHandle,  RHIResourceState::UnorderedAccess } },
        [this, &context](const EnhancedRenderGraph::ExecuteContext& executeContext)
        {

            const uint32_t lightCount =
                static_cast<uint32_t>(context.lights->size());

            // 광원 배열 업로드. EnhancedLight가 float4 넷이므로 그대로 복사한다.
            // 0개여도 최소 한 칸은 할당한다 — 빈 할당은 링이 거절한다.
            const uint64_t lightBytes =
                static_cast<uint64_t>((0 == lightCount) ? 1 : lightCount)
                * sizeof(EnhancedLight);
            const auto lightUpload = context.resources->AllocateUpload(
                RHIUploadRequest{ lightBytes, RHIUploadUsage::BufferCopy, 16 });
            if (!lightUpload.IsValid()) return;
            if (0 != lightCount)
            {
                memcpy(lightUpload.cpuAddress, context.lights->data(),
                    static_cast<size_t>(lightCount) * sizeof(EnhancedLight));
            }

            CullParams params{};
            if (nullptr != context.camera)
            {
                params.view = math::transpose(context.camera->view);
                params.inverseProjection =
                    math::transpose(context.camera->inverseProjection);
            }
            params.screenWidth = context.width;
            params.screenHeight = context.height;
            params.tileGridX = m_tileCountX;
            params.tileGridY = m_tileCountY;
            params.lightCount = lightCount;

            const auto cb = context.resources->UploadConstants(
                &params, sizeof(CullParams));
            if (!cb.IsValid()) return;
            const uint32_t tileTotal = m_tileCountX * m_tileCountY;

            const RHIBindingDesc srvs[] = {
                RHIBindingDesc::SrvDepth(executeContext.ResolveHandle(m_inputs.depth)),
            };
            const RHIBindingDesc uavs[] = {
                RHIBindingDesc::UavBuffer(m_tileCountBuffer,
                    tileTotal * 2, sizeof(uint32_t)),
                RHIBindingDesc::UavBuffer(m_tileListBuffer,
                    tileTotal * kMaxLightsPerTile, sizeof(uint32_t)),
            };
            const RHIBindingTable srvTable = context.resources->CreateBindings(srvs);
            const RHIBindingTable uavTable = context.resources->CreateBindings(uavs);
            if (!srvTable.IsValid() || !uavTable.IsValid()) return;

            RHIEncoder& encoder = *executeContext.encoder;
            encoder.SetPipeline(RHIBindPoint::Compute, m_cullPSO);
            encoder.SetConstantBuffer(RHIBindPoint::Compute, 0, cb);
            encoder.SetBindings(RHIBindPoint::Compute, 1, srvTable);
            encoder.SetRootBuffer(RHIBindPoint::Compute, 2, lightUpload);
            encoder.SetBindings(RHIBindPoint::Compute, 3, uavTable);

            encoder.Dispatch(m_tileCountX, m_tileCountY, 1);

            // ★ 여기 있던 UavBarrier가 사라졌다. 손으로 걸어야 했던 이유는
            //   타일 버퍼가 그래프 밖 리소스여서였는데, 이제 그래프가 알고
            //   있으므로 다음 소비자의 usage 선언이 UAV → SRV/CopySource
            //   전이를 만들고 그 전이가 이 디스패치의 쓰기 완료를 함의한다.
        },
        // 결과(타일 버퍼)가 그래프 밖 리소스라 컬링이 이 패스를 못 본다 —
        // 뿌리로 표시해야 걷어내지지 않는다. 셰이딩 출력도 지금은 소비자가
        // 없으므로 둘 다 뿌리다. 후처리가 붙으면 셰이딩만 남기고 뗀다.
        true);

    // 그릴 포워드 물체가 없으면 셰이딩을 선언하지 않는다.
    //
    // 빈 패스를 도는 낭비를 아끼는 것이 아니라, 선언 자체가 깊이를 DepthWrite로
    // 요구하기 때문이다 — 깊이 텍스처가 DSV로 만들어지지 않은 경로(컬링만 쓰는
    // 자가 검증 등)에서는 그 요구가 곧 검증 레이어 오류이자 디바이스 제거다.
    // 실제로 컬링 검증이 이것으로 죽었다.
    if (!m_shadePipelineRequest.IsValid() ||
        nullptr == context.forwardDraws || context.forwardDraws->empty())
    {
        return;
    }

    // ── 포워드 셰이딩 ──
    //
    // 타일 버퍼를 UAV에서 SRV로 넘긴다. 그래프가 버퍼를 모르므로 배리어를
    // 여기서 직접 건다 — 빼먹으면 셰이딩이 컬링 이전의 값을 읽는다.
    // ── 어디에 그리는가 ──
    //
    // lighting이 주어지면 그 위에 직접 그린다. 투명은 배경과 섞여야 하는데,
    // 별도 타깃에 그려 놓고 나중에 합성하려면 프리멀티플라이드 알파를
    // 따로 관리해야 하고 겹친 투명면의 누적 알파가 어긋난다 — 실제 배경에
    // 바로 블렌드하면 그 문제가 통째로 사라진다.
    //
    // 주지 않으면 예전처럼 자기 타깃을 만들어 지운다. 자가 검증들이 그
    // 경로를 쓴다(그쪽은 배경이 없어야 픽셀 대조가 성립한다).
    const bool drawsOntoLighting = m_inputs.lighting.IsValid();
    if (drawsOntoLighting)
    {
        m_output = m_inputs.lighting;
    }
    else
    {
        RGTextureDesc outputDesc{};
        outputDesc.width = context.width;
        outputDesc.height = context.height;
        outputDesc.format = kOutputFormat;
        outputDesc.allowRenderTarget = true;
        outputDesc.name = "Forward+.Shade";
        m_output = graph.CreateTexture(outputDesc);
    }

    // 깊이는 DepthWrite로 선언하되 PSO의 쓰기 마스크가 0이라 실제로는 쓰지
    // 않는다. DepthRead로 낮추려면 DSV를 READ_ONLY_DEPTH로 만들어야 하는데
    // (RHIResourceState의 계약), 자가 검증까지 함께 바뀌므로 그대로 둔다 —
    // DEPTH_WRITE는 상위 상태라 읽기만 하는 것에 문제가 없다.
    // 그림자 맵은 있을 때만 선언한다. 없는데 선언하면 그래프가 무효 핸들을
    // 전이 대상으로 삼고, 자가 검증 경로는 애초에 그림자 패스가 없다.
    std::vector<EnhancedRenderGraph::RGPassUsage> shadeUsages = {
        { m_output, RHIResourceState::RenderTarget },
        { m_inputs.depth, RHIResourceState::DepthWrite },
        // 컬링이 쓴 것을 읽는다. 이 선언이 UAV → SRV 전이를 만들고,
        // 그 전이가 컬링의 쓰기가 끝났음을 함의한다.
        { m_tileCountHandle, RHIResourceState::ShaderResource },
        { m_tileListHandle,  RHIResourceState::ShaderResource },
    };
    const bool hasShadowMap = m_shadowMap.IsValid();
    if (hasShadowMap)
    {
        shadeUsages.push_back({ m_shadowMap, RHIResourceState::ShaderResource });
    }

    graph.AddPass("Forward+.Shade", shadeUsages,
        [this, &context, drawsOntoLighting, hasShadowMap](
            const EnhancedRenderGraph::ExecuteContext& executeContext)
        {
            const uint32_t lightCount = (nullptr == context.lights)
                ? 0u : static_cast<uint32_t>(context.lights->size());

            // 깊이는 GBuffer가 채운 것을 그대로 쓴다. 지우지 않는다 —
            // 지우면 이미 그려진 불투명 기하가 포워드 물체를 가리지 못한다.
            const RHITextureHandle colors[] = { executeContext.ResolveHandle(m_output) };
            const auto depthDesc = RHIDepthTargetDesc::Depth(
                executeContext.ResolveHandle(m_inputs.depth), kDepthFormat);
            const auto targets = context.resources->CreateRenderTargets(colors, &depthDesc);
            if (!targets.IsValid()) return;

            RHIEncoder& encoder = *executeContext.encoder;
            encoder.SetViewportAndScissor(context.width, context.height);
            encoder.BindRenderTargets(targets);

            // 라이팅 위에 그릴 때는 지우면 안 된다 — 지우는 순간 배경이
            // 사라져 투명이 섞일 대상이 없어진다.
            if (!drawsOntoLighting)
            {
                constexpr float kZero[4] = { 0.f, 0.f, 0.f, 0.f };
                encoder.ClearRenderTargets(targets, kZero);
            }

            const bool drew = RecordShading(encoder, context, lightCount,
                hasShadowMap ? executeContext.ResolveHandle(m_shadowMap) : RHITextureHandle{});
            (void)drew;

            // 끝 상태를 손으로 되돌리지 않는다. '그래프가 끝나면 타일 버퍼는
            // UAV'라는 옛 불변은 소비자들이 before=UAV를 단정하는 손 배리어를
            // 걸 수 있게 하려던 것인데, 이제 그 소비자들이 전부 usage를
            // 선언하므로 불변 자체가 필요 없다. 실제 끝 상태는 writeback이
            // 멤버에 적는다.
        },
        true);
}

// 드로우 기록. 컬링 경로와 참조 경로가 이 함수를 공유한다 — 대조가 뜻을
// 가지려면 PSO 말고는 아무것도 달라선 안 된다. 같은 것을 두 곳에서 따로
// 기록하면 그 차이가 결과에 섞여 무엇이 원인인지 알 수 없게 된다.
bool EnhancedForwardPass::RecordShading(RHIEncoder& encoder,
    const EnhancedFrameContext& context, uint32_t lightCount,
    RHITextureHandle shadowResource)
{
    if (!m_shadePipelineRequest.IsValid() || !m_referencePipelineRequest.IsValid()
        || nullptr == context.forwardDraws || context.forwardDraws->empty()
        || m_batches.empty()) return false;

    // 광원 배열. 컬링이 올린 것과 같은 내용이지만 다시 올린다 — 컬링의
    // 할당을 셰이딩까지 들고 오면 두 패스가 링 수명으로 묶인다.
    const uint64_t lightBytes =
        static_cast<uint64_t>((0 == lightCount) ? 1 : lightCount) * sizeof(EnhancedLight);
    const auto lightUpload = context.resources->AllocateUpload(
        RHIUploadRequest{ lightBytes, RHIUploadUsage::BufferCopy, 16 });
    if (!lightUpload.IsValid()) return false;
    if (0 != lightCount)
    {
        memcpy(lightUpload.cpuAddress, context.lights->data(),
            static_cast<size_t>(lightCount) * sizeof(EnhancedLight));
    }

    // 인스턴스는 한 번에 올리고 드로우마다 주소만 옮긴다. 루트 SRV는 주소를
    // 받으므로 드로우별 재업로드가 필요 없다.
    const size_t drawCount = context.forwardDraws->size();
    const auto instanceUpload = context.resources->AllocateUpload(
        RHIUploadRequest{ drawCount * sizeof(ShadeInstance),
            RHIUploadUsage::BufferCopy, sizeof(ShadeInstance) });
    if (!instanceUpload.IsValid()) return false;

    auto* instances = static_cast<ShadeInstance*>(instanceUpload.cpuAddress);
    std::vector<MaterialView> materials(drawCount);
    std::string materialError;
    for (size_t i = 0; i < drawCount; ++i)
    {
        const EnhancedDrawItem& draw = (*context.forwardDraws)[i];
        MaterialView& material = materials[i];
        if (!ResolveMaterialView(draw, material, materialError)) return false;
        instances[i].world = math::transpose(draw.worldMatrix);
        instances[i].baseColor = material.baseColorFactor.rgba();
        instances[i].metallic = material.metallic;
        instances[i].roughness = material.roughness;
        instances[i].useNormalMap = material.useNormalMap;

        const MaterialKey key = MakeMaterialKey(draw);
        constexpr uint32_t kHasMaterialTextures = 1u << 0u;
        constexpr uint32_t kHasShaderMetaSnapshot = 1u << 1u;
        instances[i].materialFlags =
            (m_materialTextures.find(key) != m_materialTextures.end()
                ? kHasMaterialTextures : 0u)
            | (material.snapshot ? kHasShaderMetaSnapshot : 0u);
        if (material.snapshot)
        {
            const EnhancedForwardMaterialFlowSnapshot& flow =
                material.snapshot->flow;
            instances[i].flowWindVector = flow.windVector;
            instances[i].flowUvScroll = flow.uvScroll;
            instances[i].flowTotalSeconds = flow.totalSeconds;
            instances[i].flowDeltaSeconds = flow.deltaSeconds;
        }
    }

    ShadeParams params{};
    if (nullptr != context.camera)
    {
        params.viewProjection = math::transpose(
            context.camera->view * context.camera->projection);
    }
    params.tileGridX = m_tileCountX;
    params.tileGridY = m_tileCountY;
    params.lightCount = lightCount;
    if (nullptr != context.camera)
    {
        const math::vector3& eye = context.camera->eyePosition;
        params.eyePosition = math::vector4{ eye.x, eye.y, eye.z, 1.f };
    }
    const bool hasIbl = m_iblIrradiance.IsValid()
        && m_iblPrefiltered.IsValid() && m_iblBrdfLut.IsValid();
    params.hasIbl = hasIbl ? 1u : 0u;

    // 그림자. 자원이 없으면 데이터가 있어도 끈다 — 셰이더가 널 SRV를 읽어
    // 0을 얻으면 온 세상이 그늘이 된다.
    const bool hasShadow = m_shadowData.enabled && shadowResource.IsValid();
    if (hasShadow)
    {
        for (uint32_t i = 0; i < kShadowCascadeCount; ++i)
        {
            params.lightViewProjection[i] =
                math::transpose(m_shadowData.lightViewProjection[i]);
        }
        params.cameraForward = m_shadowData.cameraForward;
        params.cascadeSplits = m_shadowData.splitDepths;
        params.shadowBias = m_shadowData.bias;
        params.cascadeBlendBand = m_shadowData.cascadeBlendBand;
    }
    params.hasShadow = hasShadow ? 1u : 0u;

    const auto cb = context.resources->AllocateUpload(
        RHIUploadRequest{ sizeof(ShadeParams), RHIUploadUsage::ConstantBuffer, 1 });
    if (!cb.IsValid()) return false;
    memcpy(cb.cpuAddress, &params, sizeof(params));

    // root CBV/table을 걸기 전에 첫 material PSO로 layout을 확정한다. 이후
    // batch별 PSO는 같은 layout이라 DX12 root arguments와 Vulkan descriptor
    // pending state를 보존한다.
    const RHIPipelineHandle firstPipeline = m_useReferencePath
        ? m_batches.front().referencePipeline : m_batches.front().shadePipeline;
    if (!firstPipeline.IsValid()) return false;
    encoder.SetPipeline(RHIBindPoint::Graphics, firstPipeline);
    encoder.SetPrimitiveTopology(RHIPrimitiveTopology::TriangleList);

    encoder.SetConstantBuffer(RHIBindPoint::Graphics, 0, cb);
    encoder.SetRootBuffer(RHIBindPoint::Graphics, 2, lightUpload);
    encoder.SetRootBuffer(RHIBindPoint::Graphics, 3,
        RHIBufferSlice::Whole(m_tileCountBuffer));
    encoder.SetRootBuffer(RHIBindPoint::Graphics, 4,
        RHIBufferSlice::Whole(m_tileListBuffer));

    encoder.SetSamplers(RHIBindPoint::Graphics, 7, m_sampler);

    // IBL 셋도 드로우 밖에서 한 번. 재질과 달리 프레임 내내 같다.
    //
    // 없으면 널 디스크립터를 깐다 — 링에서 잘라 온 자리는 초기화되지 않은
    // 쓰레기라, 셰이더가 gHasIbl로 안 읽더라도 유효한 디스크립터가 있어야
    // 한다(Deferred가 같은 이유로 같은 처리를 한다).
    //
    // 포맷은 Deferred가 쓰는 것과 같아야 한다 — 생성기가 만든 리소스의
    // 실제 포맷이라 여기서만 다르게 적으면 어긋난 뷰가 된다.
    {
        // ★ 실패하면 그린 뒤가 아니라 여기서 접는다.
        //
        //   SetGraphicsRootSignature가 직전에 모든 루트 인자를 무효화했으므로,
        //   이 테이블을 안 걸고 드로우로 넘어가면 루트 파라미터 6이 바인딩되지
        //   않은 채로 그린다 — 검증 레이어가 잡는 미초기화 루트 인자이고,
        //   실제 하드웨어에서는 쓰레기 디스크립터 주소를 읽는다.
        //   위의 링 할당들(광원·인스턴스·상수)과 같은 처리다.
        constexpr RHIFormat kIblFormat = RHIFormat::RGBA16Float;

        const RHIBindingDesc frameSrvs[] = {
            RHIBindingDesc::SrvCube(hasIbl ? m_iblIrradiance : RHITextureHandle{},
                kIblFormat, 1).OrNull(),
            RHIBindingDesc::SrvCube(hasIbl ? m_iblPrefiltered : RHITextureHandle{},
                kIblFormat, hasIbl ? m_iblPrefilterMips : 1).OrNull(),
            RHIBindingDesc::Srv2D(hasIbl ? m_iblBrdfLut : RHITextureHandle{}, kIblFormat).OrNull(),
            // 그림자 맵. 깊이 배열이라 포맷과 차원을 둘 다 바꿔 봐야 한다
            // (Deferred가 같은 설명으로 같은 자원을 읽는다).
            RHIBindingDesc::SrvArray(hasShadow ? shadowResource : RHITextureHandle{},
                RHIFormat::R32Float, kShadowCascadeCount).OrNull(),
        };
        const RHIBindingTable frameTable = context.resources->CreateBindings(frameSrvs);
        if (!frameTable.IsValid()) return false;

        encoder.SetBindings(RHIBindPoint::Graphics, 6, frameTable);
    }

    bool drewAnything = false;
    for (const DrawBatch& batch : m_batches)
    {
        if (0u == batch.drawCount) continue;
        const RHIPipelineHandle pipeline = m_useReferencePath
            ? batch.referencePipeline : batch.shadePipeline;
        if (!pipeline.IsValid()) continue;
        const auto geometry = m_drawGeometry.find(batch.mesh);
        if (geometry == m_drawGeometry.end() || !geometry->second.IsValid()) continue;
        const RHIMeshBinding& entry = geometry->second;
        encoder.SetPipeline(RHIBindPoint::Graphics, pipeline);

        const bool hasSnapshot = batch.material.snapshot
            && !batch.material.snapshot->propertyBytes.empty();
        const void* materialData = hasSnapshot
            ? static_cast<const void*>(batch.material.snapshot->propertyBytes.data())
            : static_cast<const void*>(&kForwardLegacyMaterialConstants);
        const std::size_t materialSize = hasSnapshot
            ? batch.material.snapshot->propertyBytes.size()
            : sizeof(kForwardLegacyMaterialConstants);
        const auto materialConstants = context.resources->UploadConstants(
            materialData, materialSize);
        if (!materialConstants.IsValid()) continue;
        encoder.SetConstantBuffer(RHIBindPoint::Graphics, 8, materialConstants);

        // ── 재질 텍스처 ──
        //
        // 테이블은 드로우마다 건다. 텍스처가 없는 드로우에도 걸어 두는 이유는
        // 바인딩된 테이블의 디스크립터는 '초기화돼 있어야' 하기 때문이다 —
        // 셰이더가 안 읽더라도(hasBaseColorMap=0) 비워 두면 규약 위반이다.
        // 널 리소스 SRV는 유효한 디스크립터이고 읽으면 0을 준다.
        //
        // ★ 링은 프레임당 4096개를 전 패스가 나눠 쓴다. 여기서 소진되면
        //   남은 투명이 조용히 빠지는 데서 끝나지 않고, 뒤에 오는 패스
        //   (UI·기즈모)가 예산 부족을 겪어 엉뚱한 자리에서 증상이 난다 —
        //   투명이 많아지면 DX12DescriptorRecycler의 overflows를 볼 것.
        {
            const auto found = m_materialTextures.find(batch.material);

            // 없는 슬롯은 널 디스크립터다(OrNull) — 셰이더가 hasXxxMap으로
            // 안 읽더라도 테이블 칸은 초기화돼 있어야 한다.
            const auto slotDesc = [&](uint32_t slot)
            {
                const bool has = (found != m_materialTextures.end())
                    && found->second.resources[slot].IsValid();
                return RHIBindingDesc::Srv2D(
                    has ? found->second.resources[slot] : RHITextureHandle{},
                    has ? found->second.formats[slot] : RHIFormat::RGBA8Unorm,
                    0, has ? found->second.mipLevels[slot] : 1).OrNull();
            };
            const RHIBindingDesc materialSrvs[] = {
                slotDesc(0), slotDesc(1), slotDesc(2), slotDesc(3) };
            const RHIBindingTable srvTable =
                context.resources->CreateBindings(materialSrvs);
            if (!srvTable.IsValid()) break;

            encoder.SetBindings(RHIBindPoint::Graphics, 5, srvTable);
        }

        encoder.SetRootBuffer(RHIBindPoint::Graphics, 1,
            instanceUpload.SubRange(
                static_cast<uint64_t>(batch.firstDraw) * sizeof(ShadeInstance),
                static_cast<uint64_t>(batch.drawCount) * sizeof(ShadeInstance)));

        encoder.SetVertexBuffer(entry.vertices, entry.vertexStride);
        encoder.SetIndexBuffer(entry.indices, entry.indexFormat);
        encoder.DrawIndexed(entry.indexCount, batch.drawCount);
        drewAnything = true;
    }

    return drewAnything;
}

void EnhancedForwardPass::Shutdown()
{
    m_tileCountBuffer = {};
    m_tileListBuffer = {};
    m_allocatedTiles = 0;
    m_tileCountHandle = RGHandle{};
    m_tileListHandle = RGHandle{};
    m_tileCountState = RHIResourceState::Common;
    m_tileListState = RHIResourceState::Common;
    m_tileCountX = 0;
    m_tileCountY = 0;
    m_lastCulledLights = 0;
    m_lastOverflowTiles = 0;
    m_lastDrawCount = 0;
    m_lastMaterialCount = 0;
    m_lastBatchCount = 0;

    m_cullPSO = {};
    m_shadePipelineRequest = {};
    m_referencePipelineRequest = {};
    m_shaderMetaHandle = {};
    m_defaultPermutationKey = {};
    m_defaultKeywordSelections.clear();
    m_shaderBindingLayout.reset();
    m_shaderVariants.clear();

    m_materialTextures.clear();
    m_drawGeometry.clear();
    m_batches.clear();
    m_sampler = {};

    m_iblIrradiance = {};
    m_iblPrefiltered = {};
    m_iblBrdfLut = {};
    m_iblPrefilterMips = 1;

    m_shadowMap = RGHandle{};
    m_shadowData = {};
}


