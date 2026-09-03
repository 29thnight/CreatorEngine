#pragma once

#include <string>

namespace RenderTest
{
    // MBC5 — fixture project의 generation 1→2를 읽어 closure, immutable
    // aggregate, {ModelId,generation} cache 교체/retire, 실패 원자성을 검증한다.
    [[nodiscard]] bool RunModelAssetGenerationSelfTest(
        const std::string& projectRoot, std::string& outLog);

    // MBC4가 게시한 현재 corpus의 canonical sidecar/generation을 cold-load한다.
    [[nodiscard]] bool RunModelAssetGenerationCorpusSelfTest(
        const std::string& runtimeContentRoot, std::string& outLog);
}
