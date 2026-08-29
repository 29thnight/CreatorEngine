#include "ExperimentParity/ExperimentMaterialCodecSelfTest.h"

#include "Experiment/AssetIdentity.h"
#include "Experiment/MaterialAuthoringCodec.h"

#include <yaml-cpp/yaml.h>

#include <cstdio>
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

        [[nodiscard]] bool ValueEquals(
            const experiment::MaterialPropertyValue& left,
            const experiment::MaterialPropertyValue& right)
        {
            if (left.index() != right.index()) return false;
            if (const auto* value = std::get_if<bool>(&left))
                return *value == std::get<bool>(right);
            if (const auto* value = std::get_if<std::int32_t>(&left))
                return *value == std::get<std::int32_t>(right);
            if (const auto* value = std::get_if<std::uint32_t>(&left))
                return *value == std::get<std::uint32_t>(right);
            if (const auto* value = std::get_if<float>(&left))
                return *value == std::get<float>(right);
            if (const auto* value = std::get_if<math::vector2>(&left))
            {
                const auto& other = std::get<math::vector2>(right);
                return value->x == other.x && value->y == other.y;
            }
            if (const auto* value = std::get_if<math::vector3>(&left))
            {
                const auto& other = std::get<math::vector3>(right);
                return value->x == other.x && value->y == other.y
                    && value->z == other.z;
            }
            if (const auto* value = std::get_if<math::vector4>(&left))
            {
                const auto& other = std::get<math::vector4>(right);
                return value->x == other.x && value->y == other.y
                    && value->z == other.z && value->w == other.w;
            }
            if (const auto* value = std::get_if<std::string>(&left))
                return *value == std::get<std::string>(right);
            if (const auto* value =
                std::get_if<experiment::TextureReference>(&left))
            {
                // logicalName/fallbackPath는 정본이 아니라 저장되지 않는다 —
                // 보존 대상 필드만 비교한다.
                const auto& other = std::get<experiment::TextureReference>(right);
                return value->assetId == other.assetId
                    && value->colorSpace == other.colorSpace;
            }
            return false;
        }

        [[nodiscard]] bool RoundTrip(const experiment::Material& material,
            experiment::Material& outRestored, std::string& outDumped,
            std::string& outError)
        {
            YAML::Node node;
            if (!experiment::SerializeMaterialAuthoring(material, node, outError))
                return false;
            outDumped = YAML::Dump(node);
            YAML::Node reloaded;
            try { reloaded = YAML::Load(outDumped); }
            catch (const std::exception& parse)
            {
                outError = std::string("YAML reload 실패: ") + parse.what();
                return false;
            }
            return experiment::DeserializeMaterialAuthoring(reloaded,
                outRestored, outError);
        }

        void CheckDeserializeRejected(Checker& check, const std::string& yaml,
            const std::string& what)
        {
            YAML::Node node;
            try { node = YAML::Load(yaml); }
            catch (const std::exception&)
            {
                check.Check(false, what + " — fixture YAML 자체가 깨졌다");
                return;
            }
            experiment::Material restored;
            std::string error;
            const bool ok = experiment::DeserializeMaterialAuthoring(node,
                restored, error);
            check.Check(!ok, what + " — 거부해야 한다");
            check.Check(!error.empty(), what + " — 거부 사유가 있어야 한다");
        }

        inline constexpr const char* kShaderGuid =
            "11111111-1111-4111-8111-111111111111";

        [[nodiscard]] std::string MinimalYaml(const std::string& propertyLine)
        {
            return
                "schema: 1\n"
                "assetId: 00000000-0000-0000-0000-000000000000\n"
                "shaderAssetId: " + std::string(kShaderGuid) + "\n"
                "name: Probe\n"
                "blendMode: opaque\n"
                "properties:\n"
                + propertyLine +
                "keywords: []\n"
                "keywordSelections: []\n";
        }
    }

    bool RunExperimentMaterialCodecSelfTest(std::string& outLog)
    {
        Checker check{ outLog };
        outLog += "[experiment.matcodec] 합성 검사\n";

        const experiment::AssetId shaderId = MakeAssetId(kShaderGuid);
        const experiment::AssetId materialId =
            MakeAssetId("22222222-2222-4222-8222-222222222222");
        const experiment::AssetId textureId =
            MakeAssetId("33333333-3333-4333-8333-333333333333");

        // ── 1. 변이 대안 9종 왕복 identity ────────────────────────────────
        {
            experiment::Material material;
            material.assetId = materialId;
            material.shaderAssetId = shaderId;
            material.name = "CodecProbe";
            material.blendMode = experiment::MaterialBlendMode::Transparent;
            experiment::TextureReference srgbTexture{ textureId, "unstored" };
            srgbTexture.colorSpace = experiment::TextureColorSpace::Srgb;
            experiment::TextureReference nilTexture{};
            material.properties = {
                { "flag", true },
                { "steps", std::int32_t{ -7 } },
                { "mask", std::uint32_t{ 5u } },
                { "sheen", 0.25f },
                { "uvScale", math::vector2{ 2.0f, 4.0f } },
                { "glow", math::vector3{ 0.1f, 0.2f, 0.3f } },
                { "tint", math::vector4{ 1.0f, 0.5f, 0.25f, 0.875f } },
                { "tag", std::string{ "wet" } },
                { "baseColorMap", srgbTexture },
                { "normalMap", nilTexture },
            };
            material.keywords = { "high", "on" };
            material.keywordSelections = { 1, 0 };

            experiment::Material restored;
            std::string dumped;
            std::string error;
            const bool ok = RoundTrip(material, restored, dumped, error);
            check.Check(ok, "왕복 성공 (" + error + ")");
            if (ok)
            {
                check.Check(restored.assetId == material.assetId
                    && restored.shaderAssetId == material.shaderAssetId
                    && restored.name == material.name
                    && restored.blendMode == material.blendMode,
                    "identity/이름/blendMode 왕복");
                check.Check(restored.keywords == material.keywords
                    && restored.keywordSelections == material.keywordSelections,
                    "keywords/selections 왕복");
                check.Check(restored.properties.size()
                    == material.properties.size(), "property 수 왕복");
                bool valuesEqual =
                    restored.properties.size() == material.properties.size();
                for (std::size_t index = 0;
                    valuesEqual && index < material.properties.size(); ++index)
                {
                    valuesEqual =
                        restored.properties[index].name
                            == material.properties[index].name
                        && ValueEquals(restored.properties[index].value,
                            material.properties[index].value);
                }
                check.Check(valuesEqual, "변이 대안 9종 값 왕복");

                // ── 골든 눈검산 — 정본 표기 그 자체 ──────────────────────
                check.Check(std::string::npos != dumped.find("schema: 1"),
                    "골든 — schema 버전");
                check.Check(std::string::npos != dumped.find("float4: [1, 0.5, 0.25, 0.875]"),
                    "골든 — float4 flow 표기");
                check.Check(std::string::npos != dumped.find("colorSpace: srgb"),
                    "골든 — texture colorSpace");
                check.Check(std::string::npos != dumped.find(
                    "blendMode: transparent"), "골든 — blendMode");
                check.Check(std::string::npos == dumped.find("unstored"),
                    "골든 — logicalName은 저장되지 않는다");
                check.Check(std::string::npos == dumped.find("m_propertyValues"),
                    "골든 — legacy 표기가 섞이지 않는다");
            }
        }

        // ── 2. 인라인 재질 — assetId nil 허용 ─────────────────────────────
        {
            experiment::Material material;
            material.shaderAssetId = shaderId;
            material.name = "Inline";
            experiment::Material restored;
            std::string dumped;
            std::string error;
            check.Check(RoundTrip(material, restored, dumped, error),
                "nil assetId 왕복 (" + error + ")");
            check.Check(!restored.assetId.IsValid(), "nil assetId 보존");
        }

        // ── 3. Serialize fail-closed ──────────────────────────────────────
        {
            experiment::Material material;   // shaderAssetId nil
            material.name = "NoShader";
            YAML::Node node;
            std::string error;
            check.Check(!experiment::SerializeMaterialAuthoring(material, node,
                error) && !error.empty(), "nil shaderAssetId 직렬화 거부");
        }
        {
            experiment::Material material;
            material.shaderAssetId = shaderId;
            material.properties = { { "", 1.0f } };
            YAML::Node node;
            std::string error;
            check.Check(!experiment::SerializeMaterialAuthoring(material, node,
                error), "빈 property 이름 직렬화 거부");
        }
        {
            experiment::Material material;
            material.shaderAssetId = shaderId;
            material.keywords = { "" };
            YAML::Node node;
            std::string error;
            check.Check(!experiment::SerializeMaterialAuthoring(material, node,
                error), "빈 keyword 직렬화 거부");
        }

        // ── 4. Deserialize fail-closed ────────────────────────────────────
        CheckDeserializeRejected(check,
            "schema: 2\nshaderAssetId: " + std::string(kShaderGuid) + "\n",
            "schema 버전 불일치");
        CheckDeserializeRejected(check,
            "schema: 1\nassetId: 00000000-0000-0000-0000-000000000000\n"
            "name: X\nblendMode: opaque\nproperties: []\n"
            "keywords: []\nkeywordSelections: []\n",
            "shaderAssetId 누락");
        CheckDeserializeRejected(check,
            "schema: 1\nassetId: 00000000-0000-0000-0000-000000000000\n"
            "shaderAssetId: not-a-guid\nname: X\nblendMode: opaque\n"
            "properties: []\nkeywords: []\nkeywordSelections: []\n",
            "비정규 shaderAssetId");
        CheckDeserializeRejected(check,
            "schema: 1\nassetId: 00000000-0000-0000-0000-000000000000\n"
            "shaderAssetId: " + std::string(kShaderGuid) + "\nname: X\n"
            "blendMode: add\nproperties: []\nkeywords: []\n"
            "keywordSelections: []\n",
            "미지의 blendMode");
        CheckDeserializeRejected(check,
            MinimalYaml("  - name: p\n    float: 1.0\n    int: 2\n"),
            "값 키 2개");
        CheckDeserializeRejected(check,
            MinimalYaml("  - name: p\n"),
            "값 키 0개");
        CheckDeserializeRejected(check,
            MinimalYaml("  - name: p\n    vec4: [1, 2, 3, 4]\n"),
            "미지의 값 키");
        CheckDeserializeRejected(check,
            MinimalYaml("  - name: p\n    float3: [1, 2]\n"),
            "float3 원소 수 불일치");
        CheckDeserializeRejected(check,
            MinimalYaml("  - name: p\n    texture: {guid: bad-guid, colorSpace: srgb}\n"),
            "texture 비정규 GUID");
        CheckDeserializeRejected(check,
            MinimalYaml("  - name: p\n    texture: {guid: "
                + std::string(kShaderGuid) + "}\n"),
            "texture colorSpace 누락");
        CheckDeserializeRejected(check,
            MinimalYaml("  - name: p\n    texture: {guid: "
                + std::string(kShaderGuid)
                + ", colorSpace: srgb, path: a.png}\n"),
            "texture의 미지 키");
        CheckDeserializeRejected(check,
            "schema: 1\nassetId: 00000000-0000-0000-0000-000000000000\n"
            "shaderAssetId: " + std::string(kShaderGuid) + "\nname: X\n"
            "blendMode: opaque\nproperties: []\nkeywords: []\n"
            "keywordSelections: [70000]\n",
            "uint16 범위 밖 selection");

        char summary[160]{};
        std::snprintf(summary, sizeof(summary),
            "  합성 단정 %zu/%zu\n", check.passed,
            check.passed + check.failed);
        outLog += summary;
        return check.failed == 0u;
    }
}
