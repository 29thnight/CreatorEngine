#pragma once

#include "ImportedScene.h"
#include "../ModelData.h"
#include "../../StandardMaterialProperty.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

// ImportedScene(임포트 IR) → experiment::ModelDraft(런타임 데이터 모델) 변환.
//
// ★ 이 경계가 **손실이 일어나는 유일한 지점**이다. 임포터는 source 의 진실을
//   최대한 보존하고, 런타임 모델이 표현하지 못하는 것은 전부 여기서 버려지며
//   그때마다 ImportNote 로 계수된다. 임포터 구현 안에서 조용히 사라지는 것이
//   없도록 하는 것이 ImportedScene 계층을 따로 둔 이유다.
//
// 여기서 집행하는 손실 목록:
//   - joint 가 아닌 node 를 타깃하는 채널        → 탈락 + 계수
//   - influence 5개 이상                         → 상위 4개 + 재정규화 + 계수
//   - skin 이 둘 이상                            → 첫 skin 만 + 계수
//   - CubicSpline 보간                           → Linear 강등 + 계수
//     (Step 은 손실이 아니다 — InterpolationMode 로 그대로 실려 간다)
//   - 같은 tick 에 뭉친 키                       → 하나만 남김 + 계수
//   - PBR semantic → shader property 이름        → 정책 매핑(미매핑 시 계수)
//   - 루트 여럿                                  → 합성 루트 하나로 접기
//   - 초 → tick                                  → ticksPerSecond 로 환산
namespace experiment::importer
{
    // PBR semantic 을 shader property 이름으로 옮기는 정책. ShaderMeta 가
    // 기대하는 이름은 프로젝트 규약이므로 데이터가 아니라 옵션으로 받는다.
    struct MaterialPropertyNames final
    {
        std::string baseColorFactor{ standard_material::property::BaseColor };
        std::string metallicFactor{ standard_material::property::Metallic };
        std::string roughnessFactor{ standard_material::property::Roughness };
        std::string emissiveFactor{ standard_material::property::Emissive };
        std::string emissiveStrength{ standard_material::property::EmissiveStrength };
        std::string normalScale{ standard_material::property::NormalScale };
        std::string occlusionStrength{
            standard_material::property::OcclusionStrength };
        std::string alphaCutoff{ standard_material::property::AlphaCutoff };

        std::string baseColorMap{ standard_material::property::BaseColorMap };
        std::string metallicRoughnessMap{ standard_material::property::OrmMap };
        std::string normalMap{ standard_material::property::NormalMap };
        std::string occlusionMap{ standard_material::property::AoMap };
        std::string emissiveMap{ standard_material::property::EmissiveMap };
    };

    struct ConversionOptions final
    {
        // 모델 정체성은 .meta 소유이므로 변환기가 만들지 않고 받는다.
        AssetId modelAssetId{};
        std::string modelName{};
        // source preview용 단일 fallback. 제품 Cook은 아래 resolver로 PBR
        // alpha 정책에 맞는 실제 ShaderMeta identity를 재질마다 공급한다.
        AssetId shaderAssetId{};

        std::function<AssetId(const ImportedMaterial&, std::size_t)>
            resolveShaderAsset{};

        // material은 모델 내부 순번만으로 영속 ID를 만들지 않는다. D2 catalog/
        // authoring transaction이 발급한 identity를 D5 cook producer가 이 훅으로
        // 공급한다. 비어 있거나 nil을 반환하면 source preview draft에는 nil이
        // 남을 수 있지만 checked cooked writer는 publication을 거부한다.
        std::function<AssetId(const ImportedMaterial&, std::size_t)>
            resolveMaterialAsset{};

        MaterialPropertyNames propertyNames{};

        // 초 → tick 환산 계수. ModelDraft 가 tick 정본을 유지하는 동안 필요하다
        // (ModelDraft 도 초로 옮기면 이 옵션은 사라진다).
        double ticksPerSecond{ 30.0 };

        // 루트가 여럿일 때 합성 루트를 만든다. false 면 변환이 실패한다.
        bool synthesizeRootNode{ true };
        std::string synthesizedRootName{ "<root>" };

        // 임베디드/외부 텍스처를 자산 ID 로 푸는 훅. 비어 있으면 sourcePath 를
        // fallbackPath 로 쓰고, 그것도 없으면 논리 이름만 남기고 계수한다.
        std::function<AssetId(const ImportedTexture&)> resolveTextureAsset{};
    };

    struct ConversionResult final
    {
        std::optional<ModelDraft> draft{};
        std::vector<ImportNote> notes{};

        [[nodiscard]] bool Succeeded() const noexcept { return draft.has_value(); }
    };

    [[nodiscard]] ConversionResult ConvertToModelDraft(
        const ImportedScene& scene, const ConversionOptions& options);

    // TRS → 행 우선 4x4. 합성 순서는 S * R * T(행 벡터 규약)로, legacy
    // calculAni 및 global = local * parent 누적과 같은 의미다.
    [[nodiscard]] math::matrix4x4 ComposeTrs(const TrsTransform& transform) noexcept;
}
