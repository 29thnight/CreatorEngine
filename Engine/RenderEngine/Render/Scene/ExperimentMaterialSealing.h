#pragma once

#include "../../Experiment/MaterialResolver.h"
#include "../Graph/EnhancedRenderPass.h"

#include <cstdint>
#include <string>
#include <vector>

class Material;
struct ShaderMeta;
namespace assets { class ModelAssetGeneration; } // MBC7 — closure texture 축

// I5-M4 — M6 draw snapshot sealing의 experiment 치환.
//
// sealing이 읽는 유일한 입력은 SealSource다. legacy `::Material`은
// BuildSealSourceFromLegacy에서 **한 번** 변환되고, 그 뒤의 keyword 정규화·
// propertyBytes·textureBindings는 experiment 정본(NormalizeMaterialKeyword
// Selections·MaterialPropertyPacker 경유 BuildMaterialPropertyBlock)만 탄다.
// I5-M5가 저작 경계를 옮기면 브리지 호출부가 사라지고, 브리지는 I6에서 legacy와
// 함께 은퇴한다.
namespace ExperimentMaterialSealing
{
    struct SealTextureOwner final
    {
        std::string propertyName{};
        std::shared_ptr<Texture> owner{};
    };

    struct SealSource final
    {
        experiment::Material material{};
        // 이름→generation owner. legacy에서는 GetTextureMapShared가 채운다.
        std::vector<SealTextureOwner> textures{};
        // ShaderMeta 논리 밖의 draw 상태 — Forward snapshot 전용. flow의
        // totalSeconds/deltaSeconds는 프레임 시각이므로 seal 호출부가 채운다.
        // PBR-S3/I5-M5에서 논리 property 승격 후보다.
        EnhancedForwardMaterialFlowSnapshot flow{};
        math::color baseColorFactor{ 1.f, 1.f, 1.f, 1.f };
        float metallic{ 0.f };
        float roughness{ 1.f };
        std::uint32_t useNormalMap{ 0 };
        std::string debugName{};
    };

    // legacy → experiment 변환 브리지.
    //
    // ★ MaterialInfo 3필드 폴백(baseColor/metallic/roughness, 논리 값 부재 시)을
    //   **여기서** 승계한다 — legacy BuildShaderPropertyBlock과 같은 규칙이라
    //   sealing 교체가 bytes를 바꾸지 않는다. experiment sealing 자체는 폴백이
    //   없고(I5-M1 계약), 폴백은 legacy를 읽는 이 브리지와 함께 I6에서 죽는다.
    //
    // 변환은 meta의 property 선언(desc.type)을 기준으로 한다 — legacy 논리 값은
    // 타입 태그가 없어 desc 없이는 variant 대안을 정할 수 없다. meta가 모르는
    // legacy 값은 나르지 않는다(sealing은 meta 선언만 순회한다).
    [[nodiscard]] bool BuildSealSourceFromLegacy(const Material& legacy,
        const ShaderMeta& meta, SealSource& outSource, std::string& outError);

    // I5-D5c5 — **저작 정본만으로** seal source를 시공한다. legacy Material을
    // 아예 읽지 않는 유일한 입구다(S2c-2b의 목표). 부속의 출처 판정은 실측이다:
    //   · flow.windVector/uvScroll — ShaderMeta에 이미 승격된 논리 property
    //     (flowWindVector/flowUvScroll)에서 온다. 승격은 M5가 이미 했고 제품
    //     snapshot draw는 b2를 읽는다 — 인스턴스 채널은 legacy 폴백이다.
    //   · baseColorFactor/metallic/roughness — 같은 이름의 논리 property에서.
    //     제품 셰이더는 CB를 우선하므로(usePropertyBlock/useLegacyInstance
    //     Material) 이 값들은 legacy/self-test 채널 전용이다.
    //   · useNormalMap — **유일하게 살아 있는 인스턴스 채널 소비**다
    //     (ForwardShade:441·GBuffer:237이 무조건 읽는다). 저작 정본에서는
    //     normalMap texture가 실제로 해석됐는지로 유도한다.
    // 시각(totalSeconds/deltaSeconds)은 재질 값이 아니라 seal 호출부의 몫이다.
    // MBC7 — generation을 주면 texture 해석의 첫 축이 그 closure다(embedded
    // texture는 파일이 없어 경로 해석이 원리적으로 실패한다). nullptr이면 예전과
    // 같다(cooked → source).
    [[nodiscard]] bool BuildSealSourceFromAuthored(
        const experiment::Material& authored, const ShaderMeta& meta,
        SealSource& outSource, std::string& outError,
        const assets::ModelAssetGeneration* generation = nullptr);

    // I5-D5c2-2 — 저작 정본 직행. `BuildSealSourceFromLegacy`가 채운 SealSource의
    // **material만** 저작 원본으로 교체한다(properties·keywords·blendMode).
    // 나머지 부속(texture generation owner·flow·legacy 호환 스칼라)은 전환기
    // 동안 legacy에서 온다 — 그쪽의 정본화는 M2 resolver 배선(c3)의 몫이다.
    //
    // ★ 왜 "교체"인가: 저작 원본이 있으면 legacy를 거쳐 온 properties는 MaterialInfo
    //   3필드 폴백이 주입된 값이다(c1 실측). 저작본이 그 자리를 대신하면 누락
    //   property는 packer의 ApplyDefault(ShaderMeta 선언 기본값)가 채운다 —
    //   c2-1이 두 경로의 packing 바이트가 같음을 실측했다(sealByteMismatch=0).
    void ApplyAuthoredMaterial(SealSource& source,
        const experiment::Material& authored);

    // I5-D5c3-2 — texture generation owner를 저작 GUID에서 해석한다(M2
    // resolver의 첫 제품 소비자). 지금은 legacy 이름 맵(GetTextureMapShared)이
    // 채우는데, 그 이름 폴백은 D5-c 이주가 죽인 표면이고 cooked 우선 해석도
    // 못 탄다. 저작 정본이 있을 때만 부른다.
    //
    // fail-open이 아니다: 해석 실패는 false이고 호출부가 legacy로 내려간다 —
    // 텍스처가 조용히 빠진 그림보다 전환기 경로가 낫다(관측은 notes가 진다).
    [[nodiscard]] bool ApplyAuthoredTextures(SealSource& source,
        const ShaderMeta& meta, std::string& outError,
        std::size_t* outCooked = nullptr, std::size_t* outSourceFallback = nullptr,
        const assets::ModelAssetGeneration* generation = nullptr);

    // layout 확보 후 호출 — propertyBytes(정본 packer)와 textureBindings
    // (reflection register 검증·중복 거부, legacy SealMaterialTextureBindings와
    // 같은 규칙)를 만든다. keyword 정규화는
    // experiment::NormalizeMaterialKeywordSelections를 직접 쓴다(EnsureShaderMeta
    // Variant 전에 필요해서 이 함수 밖이다).
    [[nodiscard]] bool SealCore(const SealSource& source, const ShaderMeta& meta,
        const ShaderMetaBindingLayout& layout,
        std::vector<std::uint8_t>& outPropertyBytes,
        std::vector<EnhancedMaterialTextureBinding>& outTextureBindings,
        std::string& outError);
}
