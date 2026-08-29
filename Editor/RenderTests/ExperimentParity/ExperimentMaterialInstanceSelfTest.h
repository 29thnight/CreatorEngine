#pragma once

#include <string>

namespace RenderTest
{
    // I5-M3 — MaterialInstance 합성 검사.
    //
    // ★ 확인하는 것:
    //   1. base 불변성 — override가 base 저작 정본을 한 바이트도 바꾸지 않는다
    //   2. override 합성 — 같은 이름 갱신(축적 금지)·새 property 추가·clear 복원
    //   3. keyword override가 resolver에서 base의 같은 축 선택을 이긴다
    //   4. 효과 머테리얼의 CB bytes가 같은 값을 직접 저작한 머테리얼과
    //      비트 단위로 같다 — 인스턴스 경로가 두 번째 packer가 아니라는 증명
    //   5. InstantiateShared 계약 비승계 — 인스턴스는 base identity를 참조용으로
    //      보존할 뿐, 등록·저장 경로가 없다
    [[nodiscard]] bool RunExperimentMaterialInstanceSelfTest(std::string& outLog);
}
