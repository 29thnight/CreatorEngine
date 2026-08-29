#pragma once

#include <string>

namespace RenderTest
{
    // I5-M2 — MaterialResolver 합성 검사.
    //
    // ★ 가짜 서비스를 꽂아 **호출 계수**로 본다(experiment.resolver와 같은
    //   처방) — "결과가 맞다"만 보면 cooked를 두고 source를 도는 상태가
    //   조용히 통과한다. 확인하는 것:
    //   1. cooked 우선·source 폴백의 호출 순서 (안 불려야 할 쪽이 정말 안
    //      불리는가)
    //   2. 폴백이 관측 가능한가 — notes 계수
    //   3. keyword 정규화 — 이름이 정본으로 인덱스를 덮고, 모호/미지/범위 밖은
    //      fail-closed
    [[nodiscard]] bool RunExperimentMaterialResolveSelfTest(std::string& outLog);

    // 실사 leg — 제품 바인딩(MakeDataSystemMaterialResolveServices, catalog
    // 부재)이 실제 DataSystem 위에서 ShaderMetaFixture를 해석한다.
    [[nodiscard]] bool RunExperimentMaterialResolveReal(std::string& outLog);
}
