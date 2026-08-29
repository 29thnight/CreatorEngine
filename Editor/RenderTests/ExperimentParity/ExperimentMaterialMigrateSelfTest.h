#pragma once

#include <string>

namespace RenderTest
{
    // I5-M5 S1 — legacy ↔ experiment 변환 정본과 DataSystem 읽기 이중화 검사.
    //
    // 확인하는 것:
    //   1. 전체 사슬 왕복 — legacy → ConvertLegacyMaterial → S0 코덱
    //      직렬화/역직렬화 → ConvertToLegacyMaterial → legacy. 필드 보존과
    //      **CB bytes 비트 단위 패리티**(왕복이 화면 입력을 바꾸지 않는다).
    //   2. MaterialInfo 폴백 승계 — 논리 값 없는 legacy가 왕복 후에도 같은
    //      bytes를 내고, 역변환이 m_materialInfo 스칼라를 동기화한다.
    //   3. 이름 기반 keywords의 meta 정규화 — meta 없으면 fail-closed.
    //   4. legacy에 표현이 없는 값(string·int32 밖 uint)의 fail-closed.
    [[nodiscard]] bool RunExperimentMaterialMigrateSelfTest(std::string& outLog);

    // 실사 leg — DataSystem::DeserializeMaterialPayload가 새 정본 문서를 읽어
    // legacy 런타임 재질로 변환하고, 이름 keywords를 실제 ShaderMeta
    // (SelfTest fixture)로 정규화한다.
    [[nodiscard]] bool RunExperimentMaterialMigrateReal(std::string& outLog);
}
