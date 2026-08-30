#pragma once

#include "Experiment/ModelData.h"

#include <string>

class Material;
struct ShaderMeta;

// I5-M5 S1 — legacy `::Material` ↔ `experiment::Material` 변환의 **단일 정본**.
// 두 번째 변환기를 만들지 않는다 — M4 sealing 브리지도 여기에 위임한다.
// 이 파일은 legacy를 읽고 쓰는 전환기 경계이며 I6에서 legacy와 함께 은퇴한다.
namespace ExperimentMaterialMigration
{
    // legacy 런타임 재질 + meta → experiment 정본.
    //
    // meta.properties 선언만 순회한다 — legacy 논리 값은 타입 태그가 없어
    // desc.type 없이는 variant 대안을 정할 수 없고, meta가 모르는 legacy 값은
    // 나르지 않는다. MaterialInfo 3필드 폴백(baseColor/metallic/roughness,
    // 논리 값 부재 시)을 **여기서** 승계한다 — legacy BuildShaderPropertyBlock과
    // 같은 규칙이라 변환이 CB bytes를 바꾸지 않는다.
    [[nodiscard]] bool ConvertLegacyMaterial(const Material& legacy,
        const ShaderMeta& meta, experiment::Material& outMaterial,
        std::string& outError);

    // experiment 정본 → legacy 런타임 재질. 런타임 소유가 아직 legacy인 동안
    // (S2 이전) 새 정본 파일을 소비하기 위한 어댑터다.
    //
    // fail-closed: string property는 legacy에 표현이 없다. 이름 기반 keywords는
    // meta가 있어야 인덱스로 정규화된다(없으면 실패 — 짐작해 버리면 화면이
    // 조용히 달라진다). texture의 colorSpace는 legacy에 표현이 없어 소실된다 —
    // 런타임 로드 규약(baseColorMap만 compress)이 그 역할을 대신한다.
    // baseColor/metallic/roughness 논리 값은 m_materialInfo에도 동기화한다
    // (Forward snapshot의 legacy 호환 스칼라가 이 값을 읽는다).
    [[nodiscard]] bool ConvertToLegacyMaterial(
        const experiment::Material& material, const ShaderMeta* meta,
        Material& outLegacy, std::string& outError);

    // S2c-2a — 값 하나를 legacy 재질에 적용한다(이름 upsert + 스칼라 사본
    // 동기화). ConvertToLegacyMaterial과 같은 값 변환 정본을 공유한다 —
    // 씬 embed의 base+override 로드가 이 창구로 override를 겹친다.
    [[nodiscard]] bool ApplyPropertyToLegacy(Material& legacy,
        const experiment::MaterialProperty& property, std::string& outError);

    // 논리 값 → legacy 호환 스칼라 사본(m_materialInfo·m_flowInfo) 동기화.
    // ConvertToLegacyMaterial 말미와 ApplyPropertyToLegacy가 공유한다.
    void SynchronizeLegacyScalarMirrors(Material& legacy);
}
