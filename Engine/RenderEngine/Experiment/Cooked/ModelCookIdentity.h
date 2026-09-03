#pragma once
// PHASE 3.75 MBC11 — legacy v1 model subasset sidecar 리더(`guid` + subAssets schema 1)와
// import 결과 대조(`ValidateModelCookIdentity`)는 은퇴했다. 모델 신원은 schema v2(UUIDv8,
// `Assets/ModelSidecarV2`)뿐이고 cook은 게시된 generation을 내보낸다
// (`ModelGenerationExportProducer`). 여기에는 다른 자산(texture·shadermeta·material)
// sidecar의 최상위 `guid`만 읽는 리더가 남는다.

#include "../AssetIdentity.h"

#include <string>
#include <string_view>
#include <vector>

namespace experiment::cooked
{
    enum class ModelIdentityIssueCode
    {
        InvalidDocument,
        MissingField,
        InvalidAssetId,
        DuplicateSourceKey,
        DuplicateAssetId,
        SourceMismatch,
    };

    struct ModelIdentityIssue final
    {
        ModelIdentityIssueCode code{ ModelIdentityIssueCode::InvalidDocument };
        std::string context{};
        std::string message{};
    };

    // 임의 asset sidecar의 최상위 guid만 읽는다. canonical UUIDv4 외의 표기는
    // legacy 호환 없이 거부한다(모델 sidecar는 여기로 오지 않는다 — v2 리더가 본다).
    [[nodiscard]] bool ReadAssetIdFromMeta(std::string_view yaml,
        AssetId& outAssetId, std::vector<ModelIdentityIssue>& outIssues);
}
