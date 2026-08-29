#pragma once

#include <string>

namespace RenderTest
{
    // I5-M5 S0 — experiment 저작 YAML 코덱 합성 검사.
    //
    // 확인하는 것:
    //   1. 변이 대안 9종 전부의 왕복 identity (meta 없이 — 타입 키 표기의 존재
    //      이유가 이것이다)
    //   2. 골든 눈검산 — 정본 표기가 스키마 그대로인가(왕복만 보면 "둘 다 다른
    //      표기"를 못 가른다)
    //   3. fail-closed — 비정규 GUID·값 키 0/2개·미지 키·비정규 blendMode·
    //      schema 불일치·uint16 범위 밖 selection
    [[nodiscard]] bool RunExperimentMaterialCodecSelfTest(std::string& outLog);
}
