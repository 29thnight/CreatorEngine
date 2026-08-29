#pragma once

#include <string>

namespace RenderTest
{
    // I5-M5 S3 — CLR property API의 논리 값 경로(MaterialScriptBinding) 검사.
    //
    // 확인하는 것:
    //   1. Set이 RuntimeSchema 없이 동작한다 — ConfigureShaderProperties를 한
    //      번도 부르지 않은 재질에서 논리 값이 갱신된다(완료 게이트 항목).
    //   2. 검증 기준이 ShaderMeta 선언이다 — 오타·타입 불일치·meta 부재는 false.
    //   3. metallic/roughness Set이 legacy 스칼라(m_materialInfo)를 동기화한다.
    //   4. baseColor는 논리 값 우선·사본 폴백이고 Set이 둘을 함께 갱신한다.
    //   5. InstantiateOwned가 asset cache에 등록하지 않고(비승계), 원본을
    //      변형하지 않으며, m_fileGuid는 아직 승계한다(S2c 족쇄 — 헤더 주석).
    [[nodiscard]] bool RunExperimentMaterialScriptSelfTest(std::string& outLog);

    // 실사 leg — 제품 표면(shaderMetaGuid → DataSystem meta 해석)이 실제
    // ShaderMetaFixture 위에서 동작하고, 클론이 DataSystem 캐시에 없다.
    [[nodiscard]] bool RunExperimentMaterialScriptReal(std::string& outLog);
}
