#pragma once

#include <string>

namespace RenderTest
{
    // scene/prefab producer 의 **합성** 검사.
    //
    // ★ 합성이 유일한 커버리지인 항목이 둘이다.
    //
    //   1. **scene(`.creator`) 경로.** 실자산 씬 14개에는 `.meta` 가 하나도
    //      없어서 producer 를 태울 수 없다(`.creator` 가 sidecar 대상 확장자
    //      목록에 없다). Scene kind 는 여기서만 돈다.
    //   2. **`m_textureGuid` 간선.** 실자산 씬의 인라인 재질에는
    //      `m_propertyValues` 가 0건이라 texture 간선이 실물로는 안 생긴다.
    [[nodiscard]] bool RunExperimentSceneCookSelfTest(std::string& outLog);

    // 실자산 prefab 하나.
    [[nodiscard]] bool RunExperimentSceneCookReal(
        const std::string& assetRootPath, const std::string& scenePath,
        std::string& outLog);
}
