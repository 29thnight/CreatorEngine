#pragma once

#include <string>

namespace RenderTest
{
    // I5-M1 — experiment::Material이 정본 packer로 만든 CB bytes가 legacy
    // Material::BuildShaderPropertyBlock과 비트 단위로 같은지 잰다.
    //
    // ★ 게이트가 재는 것은 packing 알고리즘이 아니라 **입력 데이터 모델**이다.
    //   정본 packer(MaterialPropertyPacker)를 양쪽이 공유하므로, 남는 변수는
    //   experiment::Material의 property(variant)가 legacy 논리 값과 같은
    //   바이트를 재현할 만큼 충분한가뿐이다. 패리티가 깨지는 자리가 곧
    //   experiment::Material의 실제 격차다.
    //
    // ★ 알려진 경계 하나를 명시적으로 못박는다: legacy의 MaterialInfo 3필드
    //   폴백(baseColor/metallic/roughness, 저작값 부재 시)은 experiment가
    //   승계하지 않는다 — 그 경우 두 경로의 바이트는 달라야 정상이고, 게이트가
    //   그 다름까지 단정한다.

    // 합성 leg — 타입 8종 전부. 손으로 짠 meta/layout이라 Slang/DataSystem이
    // 필요 없고, 패리티는 같은 meta/layout 위 두 생산자의 대조이므로 실측
    // 오프셋일 필요도 없다.
    [[nodiscard]] bool RunExperimentMaterialParitySelfTest(std::string& outLog);

    // 실사 leg — SelfTest/ShaderMetaFixture.shadermeta를 DataSystem으로 로드해
    // 실제 Slang reflection layout 위에서 같은 저작값의 양쪽 bytes를 대조한다.
    [[nodiscard]] bool RunExperimentMaterialParityReal(std::string& outLog);
}
