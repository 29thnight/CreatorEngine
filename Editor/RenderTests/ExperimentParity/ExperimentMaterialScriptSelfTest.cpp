#include "ExperimentParity/ExperimentMaterialScriptSelfTest.h"

#include "DataSystem.h"
#include "Material.h"
#include "MaterialScriptBinding.h"
#include "RHI/RHIShaderSource.h"
#include "ShaderMeta.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <span>
#include <filesystem>
#include <string>
#include <vector>

namespace RenderTest
{
    namespace
    {
        struct Checker final
        {
            std::string& log;
            std::size_t passed{};
            std::size_t failed{};

            void Check(bool condition, const std::string& what)
            {
                if (condition) { ++passed; return; }
                ++failed;
                log += "    [실패] " + what + "\n";
            }
        };

        [[nodiscard]] ShaderMeta MakeSyntheticMeta()
        {
            ShaderMeta meta;
            meta.name = "ScriptProbe";
            meta.properties = {
                { "roughness", "Roughness", ShaderPropertyType::Float, 1.0f },
                { "metallic", "Metallic", ShaderPropertyType::Float, 0.0f },
                { "tint", "Tint", ShaderPropertyType::Float4,
                  std::array<float, 4>{ 1.0f, 1.0f, 1.0f, 1.0f } },
                { "steps", "Steps", ShaderPropertyType::Int, std::int32_t{ 3 } },
                { "useFlag", "Use Flag", ShaderPropertyType::Bool, false },
            };
            return meta;
        }

        [[nodiscard]] const MaterialPropertyValue* FindValue(
            const Material& material, std::string_view name)
        {
            const auto found = std::find_if(material.m_propertyValues.begin(),
                material.m_propertyValues.end(),
                [&](const MaterialPropertyValue& candidate)
                {
                    return candidate.m_name == name;
                });
            return found == material.m_propertyValues.end() ? nullptr : &*found;
        }
    }

    bool RunExperimentMaterialScriptSelfTest(std::string& outLog)
    {
        Checker check{ outLog };
        outLog += "[experiment.matscript] 합성 검사\n";

        const ShaderMeta meta = MakeSyntheticMeta();

        // ── 1. RuntimeSchema 없이 논리 값 갱신 ────────────────────────────
        {
            Material material;   // ConfigureShaderProperties를 부르지 않는다.
            check.Check(MaterialScriptBinding::SetFloat(material, meta,
                "roughness", 0.75f), "SetFloat — schema 없이 성공");
            const MaterialPropertyValue* roughness =
                FindValue(material, "roughness");
            check.Check(nullptr != roughness
                && roughness->m_numericValue == std::vector<float>{ 0.75f },
                "논리 값 갱신");
            check.Check(material.m_materialInfo.m_roughness == 0.75f,
                "legacy 스칼라 동기화 — roughness");

            check.Check(MaterialScriptBinding::SetFloat(material, meta,
                "metallic", 0.5f)
                && material.m_materialInfo.m_metallic == 0.5f,
                "legacy 스칼라 동기화 — metallic");

            // 같은 이름 재설정 — 항목이 축적되지 않는다.
            check.Check(MaterialScriptBinding::SetFloat(material, meta,
                "roughness", 0.25f), "SetFloat 재설정");
            const std::size_t roughnessCount = static_cast<std::size_t>(
                std::count_if(material.m_propertyValues.begin(),
                    material.m_propertyValues.end(),
                    [](const MaterialPropertyValue& value)
                    {
                        return value.m_name == "roughness";
                    }));
            check.Check(1u == roughnessCount, "논리 값 항목 축적 금지");
        }

        // ── 2. Int/Bool 경로 ──────────────────────────────────────────────
        {
            Material material;
            check.Check(MaterialScriptBinding::SetInt(material, meta,
                "steps", 7), "SetInt — Int property");
            const MaterialPropertyValue* steps = FindValue(material, "steps");
            check.Check(nullptr != steps && steps->m_integerValue == 7,
                "Int 논리 값");
            check.Check(MaterialScriptBinding::SetInt(material, meta,
                "useFlag", 1), "SetInt — Bool property(legacy 관용)");
            const MaterialPropertyValue* useFlag = FindValue(material, "useFlag");
            check.Check(nullptr != useFlag && useFlag->m_boolValue,
                "Bool 논리 값");
        }

        // ── 3. fail-closed — 검증 기준은 ShaderMeta 선언이다 ──────────────
        {
            Material material;
            check.Check(!MaterialScriptBinding::SetFloat(material, meta,
                "misspelled", 1.0f), "오타 property 거부");
            check.Check(!MaterialScriptBinding::SetFloat(material, meta,
                "tint", 1.0f), "float 자리의 float4 property 거부");
            check.Check(!MaterialScriptBinding::SetInt(material, meta,
                "roughness", 1), "int 자리의 float property 거부");
            check.Check(material.m_propertyValues.empty(),
                "거부가 논리 값을 남기지 않는다");
            // 제품 표면 — shaderMetaGuid가 비면 meta 해석이 실패해야 한다.
            check.Check(!MaterialScriptBinding::SetFloat(material,
                "roughness", 1.0f), "빈 shaderMetaGuid 거부(제품 표면)");
        }

        // ── 4. baseColor — 논리 값 우선·사본 폴백 ─────────────────────────
        {
            Material material;
            material.m_materialInfo.m_baseColor = { 0.9f, 0.8f, 0.7f, 0.6f };
            const math::color fallback =
                MaterialScriptBinding::GetBaseColor(material);
            check.Check(fallback.r == 0.9f && fallback.a == 0.6f,
                "논리 값 부재 시 m_materialInfo 폴백");

            MaterialScriptBinding::SetBaseColor(material,
                { 0.2f, 0.3f, 0.4f, 1.0f });
            const MaterialPropertyValue* baseColor =
                FindValue(material, "baseColor");
            check.Check(nullptr != baseColor
                && baseColor->m_numericValue
                    == std::vector<float>{ 0.2f, 0.3f, 0.4f, 1.0f },
                "SetBaseColor 논리 값");
            check.Check(material.m_materialInfo.m_baseColor.r == 0.2f,
                "SetBaseColor 사본 동기화");
            const math::color logical =
                MaterialScriptBinding::GetBaseColor(material);
            check.Check(logical.g == 0.3f, "GetBaseColor 논리 값 우선");
        }

        // ── 4b. SetFloatVector/GetFloat/SetTexture — Inspector 경로 ──────
        {
            Material material;
            const float tint[4]{ 0.1f, 0.2f, 0.3f, 1.0f };
            check.Check(MaterialScriptBinding::SetFloatVector(material, meta,
                "tint", std::span<const float>(tint, 4)),
                "SetFloatVector — float4");
            const MaterialPropertyValue* tintValue = FindValue(material, "tint");
            check.Check(nullptr != tintValue && tintValue->m_numericValue
                == std::vector<float>{ 0.1f, 0.2f, 0.3f, 1.0f },
                "float4 논리 값");
            check.Check(!MaterialScriptBinding::SetFloatVector(material, meta,
                "tint", std::span<const float>(tint, 3)),
                "성분 수 불일치 거부");
            check.Check(!MaterialScriptBinding::SetFloatVector(material, meta,
                "misspelled", std::span<const float>(tint, 4)),
                "미지 이름 거부");
            check.Check(!MaterialScriptBinding::SetFloatVector(material, meta,
                "steps", std::span<const float>(tint, 1)),
                "float 자리의 int property 거부");

            const float roughnessOne[1]{ 0.625f };
            check.Check(MaterialScriptBinding::SetFloatVector(material, meta,
                "roughness", std::span<const float>(roughnessOne, 1))
                && material.m_materialInfo.m_roughness == 0.625f,
                "SetFloatVector 스칼라 동기화");
            check.Check(MaterialScriptBinding::GetFloat(material, "roughness",
                9.0f) == 0.625f, "GetFloat — 논리 값 우선");
            check.Check(MaterialScriptBinding::GetFloat(material, "absent",
                9.0f) == 9.0f, "GetFloat — 폴백");

            const FileGuid textureGuid = FileGuid::CreateRandomV4();
            MaterialScriptBinding::SetTexture(material, "albedoMap", textureGuid);
            const MaterialPropertyValue* albedo = FindValue(material, "albedoMap");
            check.Check(nullptr != albedo && albedo->m_textureGuid == textureGuid,
                "SetTexture — GUID 논리 값");
            MaterialScriptBinding::SetTexture(material, "albedoMap", {});
            check.Check(nullptr != FindValue(material, "albedoMap")
                && FindValue(material, "albedoMap")->m_textureGuid == FileGuid{},
                "SetTexture — nil은 텍스처 없음 저작");
        }

        // ── 5. InstantiateOwned — 비승계 클론 ─────────────────────────────
        {
            Material origin;
            origin.m_name = "ScriptOrigin";
            origin.m_fileGuid = FileGuid::CreateRandomV4();
            origin.m_materialInfo.m_roughness = 0.5f;

            const auto clone =
                MaterialScriptBinding::InstantiateOwned(origin, {});
            check.Check(nullptr != clone && clone.get() != &origin,
                "클론이 별개 인스턴스다");
            check.Check(clone && clone->m_name == "ScriptOrigin_Instance",
                "기본 이름 규칙");
            // S2c 족쇄 — m_fileGuid는 아직 승계한다(헤더 주석 참조).
            check.Check(clone && clone->m_fileGuid == origin.m_fileGuid,
                "m_fileGuid 승계(전환기 계약)");

            const auto named =
                MaterialScriptBinding::InstantiateOwned(origin, "Custom");
            check.Check(named && named->m_name == "Custom", "명시 이름");

            // 클론 변경이 원본에 닿지 않는다.
            check.Check(clone && MaterialScriptBinding::SetFloat(*clone,
                MakeSyntheticMeta(), "roughness", 0.125f),
                "클론 논리 값 갱신");
            check.Check(origin.m_propertyValues.empty()
                && origin.m_materialInfo.m_roughness == 0.5f,
                "원본 불변");
        }

        char summary[160]{};
        std::snprintf(summary, sizeof(summary),
            "  합성 단정 %zu/%zu\n", check.passed,
            check.passed + check.failed);
        outLog += summary;
        return check.failed == 0u;
    }

    bool RunExperimentMaterialScriptReal(std::string& outLog)
    {
        Checker check{ outLog };
        outLog += "[experiment.matscript] 실사 검사 — 제품 표면\n";

        const std::filesystem::path metaPath = RHIShaderSource::Resolve(
            "SelfTest/ShaderMetaFixture.shadermeta");
        const FileGuid fixtureGuid = DataSystems->GetFileGuid(metaPath);
        if (FileGuid{} == fixtureGuid)
        {
            outLog += "    [실패] ShaderMetaFixture GUID를 얻지 못했다\n";
            return false;
        }

        Material material;
        material.m_shaderMetaGuid = fixtureGuid;
        check.Check(MaterialScriptBinding::SetFloat(material, "roughness", 0.75f),
            "제품 표면 SetFloat — 실제 meta 해석");
        const MaterialPropertyValue* roughness = FindValue(material, "roughness");
        check.Check(nullptr != roughness
            && roughness->m_numericValue == std::vector<float>{ 0.75f },
            "실사 논리 값");
        check.Check(!MaterialScriptBinding::SetFloat(material, "misspelled", 1.f),
            "실사 오타 거부");

        // 클론은 asset cache의 시민이 아니다.
        material.m_name = "ScriptRealOrigin";
        const auto clone = MaterialScriptBinding::InstantiateOwned(material, {});
        check.Check(clone
            && nullptr == DataSystems->FindCachedMaterial(clone->m_name),
            "클론이 DataSystem 캐시에 등록되지 않는다");

        char summary[160]{};
        std::snprintf(summary, sizeof(summary),
            "  실사 단정 %zu/%zu\n", check.passed,
            check.passed + check.failed);
        outLog += summary;
        return check.failed == 0u;
    }
}
