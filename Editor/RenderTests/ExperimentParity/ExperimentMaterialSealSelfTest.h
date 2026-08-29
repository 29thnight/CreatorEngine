#pragma once

#include <string>

namespace RenderTest
{
    // I5-M4 — sealing 치환의 브리지 패리티 검사.
    //
    // ★ 픽셀 게이트만으로는 부족함이 변이로 증명됐다: dx12.forwardshade·
    //   vk.gbuffer의 재질은 전부 논리 값을 저작해서 MaterialInfo 폴백 경로를
    //   밟지 않는다 — 폴백을 0으로 망가뜨려도 초록이었다. 이 검사가 그 사각을
    //   직접 잰다.
    //
    // 확인하는 것:
    //   1. BuildSealSourceFromLegacy + SealCore의 propertyBytes가 legacy
    //      Material::BuildShaderPropertyBlock과 비트 단위로 같다 —
    //      (a) 논리 값 저작 재질, (b) 폴백만 있는 재질(빈 propertyValues +
    //      MaterialInfo), (c) 기본 생성 재질(defaultMaterial 형태) 전부.
    //   2. texture binding이 owner/GUID/register를 보존한다.
    //   3. keyword 정규화가 legacy 인덱스 경로와 같은 선택을 낸다.
    [[nodiscard]] bool RunExperimentMaterialSealSelfTest(std::string& outLog);
}
