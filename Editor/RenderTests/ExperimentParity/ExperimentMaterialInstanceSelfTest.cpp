#include "ExperimentParity/ExperimentMaterialInstanceSelfTest.h"

#include "Experiment/AssetIdentity.h"
#include "Experiment/MaterialInstance.h"
#include "Experiment/MaterialPropertyBlock.h"
#include "Experiment/MaterialResolver.h"
#include "ShaderMeta.h"
#include "ShaderMetaReflection.h"
#include "Texture.h"

#include <cstdio>
#include <memory>
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

        [[nodiscard]] experiment::AssetId MakeAssetId(const char* text)
        {
            experiment::AssetId id{};
            (void)experiment::TryParseCanonicalAssetId(text, id);
            return id;
        }

        [[nodiscard]] std::shared_ptr<const experiment::Material> MakeBase(
            const experiment::AssetId& materialId,
            const experiment::AssetId& shaderId)
        {
            auto base = std::make_shared<experiment::Material>();
            base->assetId = materialId;
            base->shaderAssetId = shaderId;
            base->name = "InstanceBase";
            base->properties = {
                { "tint", math::vector4{ 1.0f, 0.5f, 0.25f, 1.0f } },
                { "sheen", 0.5f },
            };
            base->keywords = { "on" };   // FOG=on
            return base;
        }

        [[nodiscard]] const experiment::MaterialProperty* FindProperty(
            const experiment::Material& material, std::string_view name)
        {
            for (const experiment::MaterialProperty& property : material.properties)
            {
                if (property.name == name) return &property;
            }
            return nullptr;
        }

        [[nodiscard]] bool PropertyEquals(const experiment::Material& material,
            std::string_view name, float expected)
        {
            const experiment::MaterialProperty* property =
                FindProperty(material, name);
            if (!property) return false;
            const auto* value = std::get_if<float>(&property->value);
            return value && *value == expected;
        }
    }

    bool RunExperimentMaterialInstanceSelfTest(std::string& outLog)
    {
        Checker check{ outLog };
        outLog += "[experiment.matinstance] 합성 검사\n";

        const experiment::AssetId materialId =
            MakeAssetId("22222222-2222-4222-8222-222222222222");
        const experiment::AssetId shaderId =
            MakeAssetId("11111111-1111-4111-8111-111111111111");

        // ── 1. base 불변성 + override 합성 ────────────────────────────────
        {
            const auto base = MakeBase(materialId, shaderId);
            experiment::MaterialInstance instance(base);
            check.Check(0u == instance.Revision(), "초기 revision 0");

            check.Check(instance.SetPropertyOverride("sheen", 0.875f),
                "override 설정");
            check.Check(1u == instance.Revision(), "Set이 revision을 올린다");
            check.Check(instance.SetPropertyOverride("glow", 0.25f),
                "base에 없는 property override");
            check.Check(!instance.SetPropertyOverride("", 1.0f),
                "빈 이름 거부");
            check.Check(2u == instance.Revision(),
                "거부된 Set은 revision을 올리지 않는다");

            experiment::Material effective;
            std::string error;
            check.Check(instance.BuildEffectiveMaterial(effective, error),
                "효과 머테리얼 생성 (" + error + ")");
            check.Check(PropertyEquals(effective, "sheen", 0.875f),
                "override 값이 base 값을 덮는다");
            check.Check(PropertyEquals(effective, "glow", 0.25f),
                "새 property가 덧붙는다");
            check.Check(nullptr != FindProperty(effective, "tint"),
                "override 안 한 base property 유지");
            check.Check(effective.properties.size() == 3u,
                "효과 property 수 — 갱신 1 + 유지 1 + 추가 1");

            // base는 한 바이트도 안 바뀌었어야 한다.
            check.Check(PropertyEquals(*base, "sheen", 0.5f)
                && base->properties.size() == 2u
                && base->keywords == std::vector<std::string>{ "on" },
                "base 불변성");

            // identity는 참조용으로 보존된다 — 인스턴스가 그 자산이라는 뜻이
            // 아니고(비승계 계약), 등록·저장 경로는 타입에 존재하지 않는다.
            check.Check(effective.assetId == materialId
                && effective.shaderAssetId == shaderId,
                "base identity 보존(해석용)");

            // 같은 이름 재설정 — 축적 금지.
            check.Check(instance.SetPropertyOverride("sheen", 0.75f),
                "같은 이름 재설정");
            check.Check(instance.PropertyOverrides().size() == 2u,
                "override 목록이 축적되지 않는다");
            experiment::Material effective2;
            check.Check(instance.BuildEffectiveMaterial(effective2, error)
                && PropertyEquals(effective2, "sheen", 0.75f),
                "재설정 값 반영");

            // clear 복원.
            check.Check(instance.ClearPropertyOverride("sheen"), "override 해제");
            check.Check(!instance.ClearPropertyOverride("missing"),
                "없는 override 해제는 거부");
            experiment::Material effective3;
            check.Check(instance.BuildEffectiveMaterial(effective3, error)
                && PropertyEquals(effective3, "sheen", 0.5f),
                "해제 뒤 base 값 복원");

            const std::uint64_t beforeClearAll = instance.Revision();
            instance.ClearAllOverrides();
            check.Check(instance.Revision() == beforeClearAll + 1u,
                "ClearAllOverrides가 revision을 올린다");
            instance.ClearAllOverrides();
            check.Check(instance.Revision() == beforeClearAll + 1u,
                "빈 상태의 ClearAllOverrides는 no-op");
        }

        // ── 2. keyword override — resolver에서 base의 같은 축을 이긴다 ────
        {
            const auto base = MakeBase(materialId, shaderId);
            experiment::MaterialInstance instance(base);
            check.Check(instance.AddKeywordOverride("off"), "keyword override");

            FileGuid shaderGuid{};
            shaderGuid.m_guid = shaderId.value;
            auto meta = std::make_shared<ShaderMeta>();
            meta->guid = shaderGuid;
            meta->keywords = { { "FOG", { "off", "on" } } };

            experiment::MaterialResolveServices services;
            services.loadShaderMetaHandle =
                [](const FileGuid&, std::string&)
                {
                    return ShaderMetaHandle{ 1, 1 };
                };
            services.resolveShaderMeta =
                [meta](const ShaderMetaHandle&)
                    -> std::shared_ptr<const ShaderMeta>
                {
                    return meta;
                };
            services.loadTexture =
                [](const std::filesystem::path&, bool)
                {
                    return std::make_shared<Texture>();
                };
            services.resolveSourcePath = [](const FileGuid&)
                {
                    return std::filesystem::path{};
                };

            experiment::Material effective;
            std::string error;
            check.Check(instance.BuildEffectiveMaterial(effective, error),
                "keyword 효과 머테리얼 생성");
            check.Check(effective.keywords
                == std::vector<std::string>{ "on", "off" },
                "override가 base keyword 뒤에 덧붙는다");

            experiment::ResolvedMaterial resolved;
            check.Check(experiment::ResolveMaterial(effective, services,
                resolved, error), "효과 머테리얼 해석 (" + error + ")");
            check.Check(resolved.keywordSelections
                == std::vector<std::uint16_t>{ 0 },
                "override(off)가 base(on)의 같은 축 선택을 이긴다");

            // 같은 값 재추가는 축적 없이 뒤로 보낸다.
            check.Check(instance.AddKeywordOverride("off"),
                "같은 keyword 재추가");
            check.Check(instance.KeywordOverrides().size() == 1u,
                "keyword override 목록이 축적되지 않는다");
            check.Check(instance.ClearKeywordOverride("off"), "keyword 해제");
            check.Check(!instance.ClearKeywordOverride("off"),
                "없는 keyword 해제는 거부");
        }

        // ── 3. CB bytes — 인스턴스 경로와 직접 저작이 비트 단위로 같다 ────
        {
            ShaderMeta meta;
            meta.name = "InstanceProbe";
            meta.properties = {
                { "tint", "Tint", ShaderPropertyType::Float4,
                  std::array<float, 4>{ 1.0f, 0.5f, 0.25f, 1.0f } },
                { "sheen", "Sheen", ShaderPropertyType::Float, 0.5f },
            };
            ShaderMetaBindingLayout layout;
            layout.constantBufferName = "MaterialProperties";
            layout.constantBufferByteSize = 32;
            {
                ShaderMetaPropertyBinding tint;
                tint.name = "tint";
                tint.propertyType = ShaderPropertyType::Float4;
                tint.resourceName = layout.constantBufferName;
                tint.byteOffset = 0;
                tint.byteSize = 16;
                ShaderMetaPropertyBinding sheen;
                sheen.name = "sheen";
                sheen.propertyType = ShaderPropertyType::Float;
                sheen.resourceName = layout.constantBufferName;
                sheen.byteOffset = 16;
                sheen.byteSize = 4;
                layout.properties = { tint, sheen };
            }

            const auto base = MakeBase(materialId, shaderId);
            experiment::MaterialInstance instance(base);
            check.Check(instance.SetPropertyOverride("sheen", 0.125f),
                "bytes 검사용 override");

            experiment::Material effective;
            std::string error;
            check.Check(instance.BuildEffectiveMaterial(effective, error),
                "bytes 검사용 효과 머테리얼");

            experiment::Material authored = *base;
            for (experiment::MaterialProperty& property : authored.properties)
            {
                if (property.name == "sheen") property.value = 0.125f;
            }

            std::vector<std::uint8_t> effectiveBytes;
            std::vector<std::uint8_t> authoredBytes;
            check.Check(experiment::BuildMaterialPropertyBlock(effective, meta,
                layout, effectiveBytes, error),
                "효과 머테리얼 packing (" + error + ")");
            check.Check(experiment::BuildMaterialPropertyBlock(authored, meta,
                layout, authoredBytes, error),
                "직접 저작 packing (" + error + ")");
            check.Check(!effectiveBytes.empty()
                && effectiveBytes == authoredBytes,
                "인스턴스 경로 CB bytes가 직접 저작과 비트 단위로 같다");
        }

        // ── 4. base 없음 — fail-closed ────────────────────────────────────
        {
            experiment::MaterialInstance empty;
            experiment::Material effective;
            std::string error;
            check.Check(!empty.BuildEffectiveMaterial(effective, error)
                && !error.empty(),
                "base 없는 인스턴스는 효과 머테리얼을 지어내지 않는다");
        }

        char summary[160]{};
        std::snprintf(summary, sizeof(summary),
            "  합성 단정 %zu/%zu\n", check.passed,
            check.passed + check.failed);
        outLog += summary;
        return check.failed == 0u;
    }
}
