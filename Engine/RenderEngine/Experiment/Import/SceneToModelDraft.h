#pragma once

#include "ImportedScene.h"
#include "../ModelData.h"

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
//   - CubicSpline 보간                           → Linear 취급 + 계수
//   - PBR semantic → shader property 이름        → 정책 매핑(미매핑 시 계수)
//   - 루트 여럿                                  → 합성 루트 하나로 접기
//   - 초 → tick                                  → ticksPerSecond 로 환산
namespace experiment::importer
{
    // PBR semantic 을 shader property 이름으로 옮기는 정책. ShaderMeta 가
    // 기대하는 이름은 프로젝트 규약이므로 데이터가 아니라 옵션으로 받는다.
    struct MaterialPropertyNames final
    {
        std::string baseColorFactor{ "_BaseColorFactor" };
        std::string metallicFactor{ "_MetallicFactor" };
        std::string roughnessFactor{ "_RoughnessFactor" };
        std::string emissiveFactor{ "_EmissiveFactor" };
        std::string normalScale{ "_NormalScale" };
        std::string occlusionStrength{ "_OcclusionStrength" };
        std::string alphaCutoff{ "_AlphaCutoff" };

        std::string baseColorMap{ "_BaseColorMap" };
        std::string metallicRoughnessMap{ "_MetallicRoughnessMap" };
        std::string normalMap{ "_NormalMap" };
        std::string occlusionMap{ "_OcclusionMap" };
        std::string emissiveMap{ "_EmissiveMap" };
    };

    struct ConversionOptions final
    {
        // 모델 정체성은 .meta 소유이므로 변환기가 만들지 않고 받는다.
        AssetId modelAssetId{};
        std::string modelName{};
        AssetId shaderAssetId{};

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
    [[nodiscard]] Matrix4 ComposeTrs(const TrsTransform& transform) noexcept;
}
