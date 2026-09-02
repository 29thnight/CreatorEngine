#pragma once

#include "ModelData.h"

#include <string>

namespace Authoring
{
    class ReadNode;
	class WriteNode;
}

namespace experiment
{
    // I5-M5 S0 — experiment::Material 저작 YAML 정본 코덱 (schema 1).
    //
    // 새 정본의 값 표기는 **타입 키가 명시된 단일 키**다:
    //   properties:
    //     - name: baseColor
    //       float4: [1, 0.5, 0.25, 1]
    //     - name: albedoMap
    //       texture: { guid: <uuid v4|nil>, colorSpace: srgb }
    // legacy m_propertyValues의 4필드 병존 표기(숫자·정수·불·GUID를 전부 싣고
    // 소비자가 desc.type으로 골라 읽는)와 달리, meta 없이 왕복 가능하다.
    //
    // ★ texture는 guid+colorSpace만 정본이다. TextureReference의 logicalName·
    //   fallbackPath는 진단/legacy 폴백 표면이라 저장하지 않는다 — D5-c 이주가
    //   죽인 이름 참조를 정본에 되살리지 않는다.
    //
    // ★ shaderAssetId는 canonical UUIDv4 필수. assetId는 씬 인라인 재질(사이드카
    //   미발급)을 위해 nil을 허용하고, 게시 게이트(D5-b2c-1)가 cooked 경계에서
    //   nil을 거부하는 분업을 유지한다.
    //
    // fail-closed: 비정규 GUID 표기·값 키 0개/2개·미지 값 키·비정규 blendMode·
    // 스키마 버전 불일치 전부 거부하고 부분 결과를 내놓지 않는다.
    inline constexpr std::uint32_t kMaterialAuthoringSchemaVersion = 1;

    [[nodiscard]] bool SerializeMaterialAuthoring(const Material& material,
		Authoring::WriteNode outNode, std::string& outError);

    [[nodiscard]] bool DeserializeMaterialAuthoring(
        const Authoring::ReadNode& node,
        Material& outMaterial, std::string& outError);

    // S2c-2a — 씬 embed의 인스턴스 override 표기가 property 값 표기(타입 키
    // 단일 키)를 재사용한다. 두 번째 표기를 만들지 않는다 — 이 둘은 문서
    // 코덱이 쓰는 내부 정본의 공개 창구다.
    [[nodiscard]] bool SerializeMaterialPropertyValue(
		const MaterialProperty& property, Authoring::WriteNode outEntry,
		std::string& outError);

    [[nodiscard]] bool DeserializeMaterialPropertyValue(
        const Authoring::ReadNode& entry,
        const std::string& name, MaterialPropertyValue& outValue,
        std::string& outError);
}
