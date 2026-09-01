#pragma once

#include "../../Experiment/MaterialResolver.h"
#include "../Graph/EnhancedRenderPass.h"

#include <cstdint>
#include <string>
#include <vector>

class Material;
struct ShaderMeta;

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
