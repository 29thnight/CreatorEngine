#pragma once

#include <cstdint>
#include <string>

namespace RenderTest
{
    // PBR-W8 — 게이트가 읽는 **수**. 로그 문장을 파싱하게 두면 문장을 다듬는 순간
    // 게이트가 조용히 아무것도 재지 않는다.
    //
    // ★ `tamperRejected`/`tamperCurrentHeld` 를 따로 센다. "실패했다" 와 "실패한
    //   뒤에도 current 가 그대로다" 는 **다른 단정**이고, W8 계약(§1 "재임포트 실패
    //   시 마지막 정상 generation 유지")이 걸린 쪽은 뒤엣것이다. 합쳐 세면 거부만
    //   되고 current 가 날아가는 회귀를 못 본다.
    struct ModelGenerationReport
    {
        std::uint64_t passed{};
        std::uint64_t failed{};
        std::uint64_t tamperCases{};        ///< 주입한 tamper 종류 수
        std::uint64_t tamperRejected{};     ///< 게시 전 거부된 수
        std::uint64_t tamperCurrentHeld{};  ///< 거부 뒤 current generation 이 불변인 수
        bool fixtureResolved{};             ///< fixture 전제(대상 ModelId · generation 두 벌)가 섰나
        std::uint64_t runtimeCases{};
        std::uint64_t runtimeRejected{};
        std::uint64_t runtimeCurrentHeld{};
        std::uint64_t runtimeTexturesHeld{};
        std::uint64_t runtimeInstanceHeld{};
        bool runtimeRecovered{}, runtimeDuplicateStable{}, runtimeRemoved{};
    };

    // MBC5 — fixture project의 generation 1→2를 읽어 closure, immutable
    // aggregate, {ModelId,generation} cache 교체/retire, 실패 원자성을 검증한다.
    // ★ `modelIdText` 로 대상을 지목한다. 예전에는 ModelId 디렉터리가 정확히
    //   1 개이기를 요구해 살아 있는 프로젝트에서 설 수 없었다(죽은 이유).
    //   generation 두 벌은 실재하는 것에서 고른다 — 번호는 watcher 때문에
    //   제어할 수 없고, 이 검사가 재는 것은 번호가 아니라 원자성이다.
    [[nodiscard]] bool RunModelAssetGenerationSelfTest(
        const std::string& projectRoot, const std::string& modelIdText,
        std::string& outLog, ModelGenerationReport* report = nullptr);

    // MBC4가 게시한 현재 corpus의 canonical sidecar/generation을 cold-load한다.
    [[nodiscard]] bool RunModelAssetGenerationCorpusSelfTest(
        const std::string& runtimeContentRoot, std::string& outLog);
}
