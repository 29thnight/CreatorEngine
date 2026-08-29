#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace asset_cooker
{
    struct ModelIdentityRefreshSummary final
    {
        std::size_t models{};
        std::size_t materials{};
        std::size_t embeddedTextures{};
    };

    // 명시적 authoring/migration 경계다. 제품 Cook은 source sidecar를 수정하지
    // 않으며, 이 함수만 현재 import 결과를 전부 확인한 뒤 model subasset
    // UUIDv4를 일괄 재발급한다. 어느 model 하나라도 준비에 실패하면 원본은
    // 전혀 바뀌지 않는다.
    [[nodiscard]] bool RefreshModelIdentities(
        const std::filesystem::path& assetRoot,
        const std::vector<std::filesystem::path>& models,
        ModelIdentityRefreshSummary& outSummary,
        std::string& outFailure);
}
