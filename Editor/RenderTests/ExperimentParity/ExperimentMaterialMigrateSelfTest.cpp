#include "ExperimentParity/ExperimentMaterialMigrateSelfTest.h"

#include "DataSystem.h"
#include "MeshRenderer.h"
#include "Experiment/AssetIdentity.h"
#include "Experiment/MaterialAuthoringCodec.h"
#include "ExperimentMaterialMigration.h"
#include "Material.h"
#include "PathFinder.h" // S2b: 실자산 corpus 경로
#include "ReflectionYml.h" // S2b: Meta::Serialize — 컴포넌트 embed 경로 실사
#include "RHI/RHIShaderSource.h"
#include "ShaderMeta.h"
#include "ShaderMetaReflection.h"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <filesystem>
#include <sstream>
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

        struct MigrateContract final
        {
            ShaderMeta meta{};
            ShaderMetaBindingLayout layout{};
        };

        [[nodiscard]] ShaderMetaPropertyBinding CbBinding(std::string name,
            ShaderPropertyType type, std::uint32_t offset, std::uint32_t size)
        {
            ShaderMetaPropertyBinding binding;
            binding.name = std::move(name);
            binding.propertyType = type;
            binding.resourceKind = RHIShaderResourceKind::ConstantBuffer;
            binding.resourceName = "MaterialProperties";
            binding.byteOffset = offset;
            binding.byteSize = size;
            return binding;
        }

        [[nodiscard]] MigrateContract MakeContract()
        {
            MigrateContract contract;
            auto& meta = contract.meta;
            meta.name = "MigrateProbe";
            meta.properties = {
                { "baseColor", "Base Color", ShaderPropertyType::Float4,
                  std::array<float, 4>{ 1.0f, 1.0f, 1.0f, 1.0f } },
                { "metallic", "Metallic", ShaderPropertyType::Float, 0.0f },
                { "roughness", "Roughness", ShaderPropertyType::Float, 1.0f },
                { "albedoMap", "Albedo Map", ShaderPropertyType::Texture2D,
                  std::monostate{} },
            };
            meta.keywords = { { "FOG", { "off", "on" } } };

            auto& layout = contract.layout;
            layout.constantBufferName = "MaterialProperties";
            layout.constantBufferByteSize = 32;
            layout.properties = {
                CbBinding("baseColor", ShaderPropertyType::Float4, 0, 16),
                CbBinding("metallic", ShaderPropertyType::Float, 16, 4),
                CbBinding("roughness", ShaderPropertyType::Float, 20, 4),
            };
            ShaderMetaPropertyBinding albedo;
            albedo.name = "albedoMap";
            albedo.propertyType = ShaderPropertyType::Texture2D;
            albedo.resourceKind = RHIShaderResourceKind::Texture;
            albedo.resourceName = "albedoMap";
            albedo.registerIndex = 3;
            layout.properties.push_back(albedo);
            return contract;
        }

        // legacy → experiment → YAML → experiment → legacy 전체 사슬.
        [[nodiscard]] bool RunFullChain(const MigrateContract& contract,
            const Material& legacy, Material& outRestored, std::string& outError)
        {
            experiment::Material converted;
            if (!ExperimentMaterialMigration::ConvertLegacyMaterial(legacy,
                contract.meta, converted, outError))
            {
                return false;
            }
            YAML::Node node;
            if (!experiment::SerializeMaterialAuthoring(converted, node,
                outError))
            {
                return false;
            }
            YAML::Node reloaded;
            try { reloaded = YAML::Load(YAML::Dump(node)); }
            catch (const std::exception& parse)
            {
                outError = std::string("YAML reload 실패: ") + parse.what();
                return false;
            }
            experiment::Material restored;
            if (!experiment::DeserializeMaterialAuthoring(reloaded, restored,
                outError))
            {
                return false;
            }
            return ExperimentMaterialMigration::ConvertToLegacyMaterial(restored,
                &contract.meta, outRestored, outError);
        }

        // Material은 복사 대입이 없어 결과를 밖으로 못 옮긴다 — 호출부가
        // restored를 소유하고, 여기서는 사슬 실행과 bytes 대조만 한다.
        [[nodiscard]] bool CheckChainBytesParity(Checker& check,
            const MigrateContract& contract, const Material& legacy,
            Material& restored, const std::string& what)
        {
            std::string error;
            const bool chained = RunFullChain(contract, legacy, restored, error);
            check.Check(chained, what + " — 사슬 왕복 (" + error + ")");
            if (!chained) return false;

            std::vector<std::uint8_t> originalBytes;
            std::vector<std::uint8_t> restoredBytes;
            check.Check(legacy.BuildShaderPropertyBlock(contract.meta,
                contract.layout, originalBytes, error),
                what + " — 원본 bytes (" + error + ")");
            check.Check(restored.BuildShaderPropertyBlock(contract.meta,
                contract.layout, restoredBytes, error),
                what + " — 왕복 bytes (" + error + ")");
            check.Check(!originalBytes.empty()
                && originalBytes == restoredBytes,
                what + " — CB bytes 비트 단위 패리티");
            return true;
        }
    }

    bool RunExperimentMaterialMigrateSelfTest(std::string& outLog)
    {
        Checker check{ outLog };
        outLog += "[experiment.matmigrate] 합성 검사\n";

        const MigrateContract contract = MakeContract();
        const FileGuid shaderGuid = FileGuid::CreateRandomV4();
        const FileGuid assetGuid = FileGuid::CreateRandomV4();
        const FileGuid textureGuid = FileGuid::CreateRandomV4();

        // ── 1. 저작 재질 전체 사슬 ────────────────────────────────────────
        {
            Material legacy;
            legacy.m_name = "MigrateAuthored";
            legacy.m_fileGuid = assetGuid;
            legacy.m_shaderMetaGuid = shaderGuid;
            legacy.m_renderingMode = MaterialRenderingMode::Transparent;
            legacy.m_keywordSelections = { 1 };
            {
                MaterialPropertyValue baseColor;
                baseColor.m_name = "baseColor";
                baseColor.m_numericValue = { 0.5f, 0.25f, 0.125f, 1.0f };
                MaterialPropertyValue metallic;
                metallic.m_name = "metallic";
                metallic.m_numericValue = { 0.75f };
                MaterialPropertyValue albedo;
                albedo.m_name = "albedoMap";
                albedo.m_textureGuid = textureGuid;
                legacy.m_propertyValues = { baseColor, metallic, albedo };
            }

            Material restored;
            (void)CheckChainBytesParity(check, contract, legacy, restored,
                "저작 재질");
            check.Check(restored.m_fileGuid == assetGuid
                && restored.m_shaderMetaGuid == shaderGuid
                && restored.m_name == "MigrateAuthored"
                && MaterialRenderingMode::Transparent == restored.m_renderingMode,
                "identity/이름/renderingMode 보존");
            check.Check(restored.m_keywordSelections
                == std::vector<std::uint16_t>{ 1 },
                "keyword 인덱스 보존");
            const auto albedoValue = std::find_if(
                restored.m_propertyValues.begin(),
                restored.m_propertyValues.end(),
                [](const MaterialPropertyValue& value)
                {
                    return value.m_name == "albedoMap";
                });
            check.Check(albedoValue != restored.m_propertyValues.end()
                && albedoValue->m_textureGuid == textureGuid,
                "texture GUID 보존");
        }

        // ── 2. MaterialInfo 폴백 재질 — 승계와 역방향 동기화 ──────────────
        {
            Material legacy;
            legacy.m_name = "MigrateFallback";
            legacy.m_shaderMetaGuid = shaderGuid;
            legacy.m_materialInfo.m_baseColor = { 0.2f, 0.3f, 0.4f, 0.6f };
            legacy.m_materialInfo.m_metallic = 0.7f;
            legacy.m_materialInfo.m_roughness = 0.25f;

            Material restored;
            (void)CheckChainBytesParity(check, contract, legacy, restored,
                "폴백 재질");
            check.Check(restored.m_materialInfo.m_baseColor.r == 0.2f
                && restored.m_materialInfo.m_baseColor.a == 0.6f
                && restored.m_materialInfo.m_metallic == 0.7f
                && restored.m_materialInfo.m_roughness == 0.25f,
                "역변환의 m_materialInfo 스칼라 동기화");
        }

        // ── 2b. flow 승격 — m_flowInfo 폴백 승계·역동기화 ─────────────────
        // 주의: legacy BuildShaderPropertyBlock에는 flow 폴백이 없다(의도 —
        // 제품 sealing은 브리지만 탄다). 그래서 여기서는 bytes 패리티가 아니라
        // 변환 자체를 단정한다.
        {
            MigrateContract flowContract = MakeContract();
            flowContract.meta.properties.push_back(
                { "flowUvScroll", "Flow UV Scroll", ShaderPropertyType::Float2,
                  std::array<float, 2>{ 0.0f, 0.0f } });
            flowContract.meta.properties.push_back(
                { "flowWindVector", "Flow Wind Vector",
                  ShaderPropertyType::Float4,
                  std::array<float, 4>{ 0.0f, 0.0f, 0.0f, 0.0f } });

            Material legacy;
            legacy.m_name = "MigrateFlow";
            legacy.m_shaderMetaGuid = shaderGuid;
            legacy.m_flowInfo.m_windVector = { 0.15f, -0.10f, 0.05f, 0.20f };
            legacy.m_flowInfo.m_uvScroll = { 0.03f, -0.02f };

            experiment::Material converted;
            std::string error;
            check.Check(ExperimentMaterialMigration::ConvertLegacyMaterial(
                legacy, flowContract.meta, converted, error),
                "flow 변환 (" + error + ")");
            const auto findProperty = [&](std::string_view name)
                -> const experiment::MaterialProperty*
            {
                for (const experiment::MaterialProperty& property
                    : converted.properties)
                {
                    if (property.name == name) return &property;
                }
                return nullptr;
            };
            const experiment::MaterialProperty* flowWind =
                findProperty("flowWindVector");
            const auto* windValue = flowWind
                ? std::get_if<math::vector4>(&flowWind->value) : nullptr;
            check.Check(nullptr != windValue && windValue->x == 0.15f
                && windValue->w == 0.20f,
                "m_flowInfo windVector가 논리 값으로 승계된다");
            const experiment::MaterialProperty* flowUv =
                findProperty("flowUvScroll");
            const auto* uvValue = flowUv
                ? std::get_if<math::vector2>(&flowUv->value) : nullptr;
            check.Check(nullptr != uvValue && uvValue->x == 0.03f
                && uvValue->y == -0.02f,
                "m_flowInfo uvScroll이 논리 값으로 승계된다");

            Material restored;
            check.Check(ExperimentMaterialMigration::ConvertToLegacyMaterial(
                converted, &flowContract.meta, restored, error),
                "flow 역변환 (" + error + ")");
            check.Check(restored.m_flowInfo.m_windVector.w == 0.20f
                && restored.m_flowInfo.m_uvScroll.y == -0.02f,
                "역변환의 m_flowInfo 동기화");
        }

        // ── 3. 이름 기반 keywords — meta 정규화·부재 시 fail-closed ───────
        {
            experiment::Material material;
            material.shaderAssetId.value = shaderGuid.m_guid;
            material.name = "KeywordProbe";
            material.keywords = { "on" };

            Material withMeta;
            std::string error;
            check.Check(ExperimentMaterialMigration::ConvertToLegacyMaterial(
                material, &contract.meta, withMeta, error),
                "keywords 정규화 (" + error + ")");
            check.Check(withMeta.m_keywordSelections
                == std::vector<std::uint16_t>{ 1 },
                "이름 keywords가 인덱스로 정규화된다");

            Material withoutMeta;
            check.Check(!ExperimentMaterialMigration::ConvertToLegacyMaterial(
                material, nullptr, withoutMeta, error) && !error.empty(),
                "meta 없는 이름 keywords는 거부해야 한다");
        }

        // ── 4. legacy에 표현이 없는 값 — fail-closed ──────────────────────
        {
            experiment::Material material;
            material.shaderAssetId.value = shaderGuid.m_guid;
            material.properties = { { "tag", std::string{ "wet" } } };
            Material converted;
            std::string error;
            check.Check(!ExperimentMaterialMigration::ConvertToLegacyMaterial(
                material, nullptr, converted, error) && !error.empty(),
                "string property는 거부해야 한다");
        }
        {
            experiment::Material material;
            material.shaderAssetId.value = shaderGuid.m_guid;
            material.properties = { { "mask", std::uint32_t{ 0x80000000u } } };
            Material converted;
            std::string error;
            check.Check(!ExperimentMaterialMigration::ConvertToLegacyMaterial(
                material, nullptr, converted, error) && !error.empty(),
                "int32 범위 밖 uint는 거부해야 한다");
        }

        char summary[160]{};
        std::snprintf(summary, sizeof(summary),
            "  합성 단정 %zu/%zu\n", check.passed,
            check.passed + check.failed);
        outLog += summary;
        return check.failed == 0u;
    }

    bool RunExperimentMaterialMigrateReal(std::string& outLog)
    {
        Checker check{ outLog };
        outLog += "[experiment.matmigrate] 실사 검사 — DataSystem 읽기 이중화\n";

        const std::filesystem::path metaPath = RHIShaderSource::Resolve(
            "SelfTest/ShaderMetaFixture.shadermeta");
        const FileGuid fixtureGuid = DataSystems->GetFileGuid(metaPath);
        if (FileGuid{} == fixtureGuid)
        {
            outLog += "    [실패] ShaderMetaFixture GUID를 얻지 못했다\n";
            return false;
        }

        const std::string yaml =
            "schema: 1\n"
            "assetId: 00000000-0000-0000-0000-000000000000\n"
            "shaderAssetId: " + fixtureGuid.ToString() + "\n"
            "name: NewCanonicalProbe\n"
            "blendMode: transparent\n"
            "properties:\n"
            "  - name: tint\n"
            "    float4: [0.125, 0.25, 0.5, 1]\n"
            "  - name: roughness\n"
            "    float: 0.75\n"
            "keywords: [high]\n"
            "keywordSelections: []\n";

        Material material;
        const bool decoded = DataSystems->DeserializeMaterialPayload(material,
            YAML::Load(yaml));
        check.Check(decoded, "새 정본 문서 decode");
        if (decoded)
        {
            check.Check(material.m_name == "NewCanonicalProbe"
                && material.m_shaderMetaGuid == fixtureGuid
                && MaterialRenderingMode::Transparent == material.m_renderingMode,
                "identity/renderingMode 변환");
            check.Check(material.m_keywordSelections
                == std::vector<std::uint16_t>{ 1 },
                "이름 keywords가 실제 ShaderMeta로 정규화된다");
            const auto tint = std::find_if(material.m_propertyValues.begin(),
                material.m_propertyValues.end(),
                [](const MaterialPropertyValue& value)
                {
                    return value.m_name == "tint";
                });
            check.Check(tint != material.m_propertyValues.end()
                && tint->m_numericValue
                    == std::vector<float>{ 0.125f, 0.25f, 0.5f, 1.0f },
                "논리 값 변환");
        }

        // ── S2-a: MeshRenderer postLoad가 새 정본 m_Material을 재해석한다 ──
        {
            const std::string componentYaml =
                "m_Material:\n"
                "  schema: 1\n"
                "  assetId: 00000000-0000-0000-0000-000000000000\n"
                "  shaderAssetId: " + fixtureGuid.ToString() + "\n"
                "  name: MeshRendererProbe\n"
                "  blendMode: opaque\n"
                "  properties:\n"
                "    - name: roughness\n"
                "      float: 0.125\n"
                "  keywords: []\n"
                "  keywordSelections: []\n";
            MeshRenderer renderer;
            renderer.OnDeserialized(YAML::Load(componentYaml));
            check.Check(nullptr != renderer.m_Material,
                "postLoad가 새 정본 material을 만든다");
            if (renderer.m_Material)
            {
                check.Check(renderer.m_Material->m_name == "MeshRendererProbe"
                    && renderer.m_Material->m_shaderMetaGuid == fixtureGuid,
                    "postLoad 재해석의 identity");
                const auto roughness = std::find_if(
                    renderer.m_Material->m_propertyValues.begin(),
                    renderer.m_Material->m_propertyValues.end(),
                    [](const MaterialPropertyValue& value)
                    {
                        return value.m_name == "roughness";
                    });
                check.Check(roughness
                    != renderer.m_Material->m_propertyValues.end()
                    && roughness->m_numericValue == std::vector<float>{ 0.125f },
                    "postLoad 재해석의 논리 값");
            }

            // legacy 노드에서는 typed가 채운 인스턴스를 보존해야 한다.
            MeshRenderer legacyRenderer;
            legacyRenderer.m_Material = std::make_shared<Material>();
            legacyRenderer.m_Material->m_name = "TypedFilled";
            const std::shared_ptr<Material> before = legacyRenderer.m_Material;
            legacyRenderer.OnDeserialized(YAML::Load("m_Material:\n  m_name: TypedFilled\n"));
            check.Check(legacyRenderer.m_Material == before
                && legacyRenderer.m_Material->m_name == "TypedFilled",
                "legacy 노드는 typed 인스턴스를 보존한다");
        }

        // legacy 문서는 기존 경로 그대로다 — 이중화가 legacy를 깨지 않는다.
        {
            Material legacyDocument;
            const bool legacyDecoded = DataSystems->DeserializeMaterialPayload(
                legacyDocument, YAML::Load(
                    "m_name: LegacyProbe\n"
                    "m_shaderMetaGuid: " + fixtureGuid.ToString() + "\n"
                    "m_propertyValues:\n"
                    "  - m_name: roughness\n"
                    "    m_numericValue: [0.5]\n"));
            check.Check(legacyDecoded
                && legacyDocument.m_name == "LegacyProbe",
                "legacy 문서 경로 무변경");
        }

        // ── S2b: writer 전환 — 저장이 새 정본을 적고 왕복이 고정점이다 ────
        {
            // 위에서 decode한 material은 fixture meta를 아는 재질이다.
            YAML::Node written = DataSystems->SerializeMaterialPayload(material);
            check.Check(written["schema"] && written["shaderAssetId"],
                "writer가 새 정본(schema+shaderAssetId)을 적는다");

            bool tintSurvived = false;
            if (written["properties"] && written["properties"].IsSequence())
            {
                for (const YAML::Node& property : written["properties"])
                {
                    if (property["name"]
                        && property["name"].as<std::string>() == "tint"
                        && property["float4"])
                    {
                        tintSurvived = true;
                    }
                }
            }
            check.Check(tintSurvived, "meta 선언 논리 값이 writer를 살아넘는다");

            // 고정점: canonical → decode → re-encode 텍스트 동일. 이게 서야
            // 씬 corpus의 save-load-resave diff 0이 새 형식에서도 성립한다.
            // decode는 DataSystem 창구 대신 그 창구가 쓰는 정본 부품(코덱+
            // 변환기+finalize)을 직접 부른다 — 같은 경로이고, 창구 시그니처가
            // D3-a 이행 중이라 여기 묶지 않는다.
            Material reloaded;
            bool redecoded = false;
            {
                experiment::Material authoredAgain;
                std::string error;
                redecoded = experiment::DeserializeMaterialAuthoring(written,
                        authoredAgain, error)
                    && ExperimentMaterialMigration::ConvertToLegacyMaterial(
                        authoredAgain, nullptr, reloaded, error);
                if (redecoded)
                {
                    DataSystems->FinalizeMaterialRuntime(reloaded);
                }
            }
            std::string firstText, secondText;
            {
                std::ostringstream stream; stream << written;
                firstText = stream.str();
            }
            if (redecoded)
            {
                YAML::Node rewritten =
                    DataSystems->SerializeMaterialPayload(reloaded);
                std::ostringstream stream; stream << rewritten;
                secondText = stream.str();
            }
            check.Check(redecoded && !firstText.empty()
                && firstText == secondText,
                "저장→로드→재저장이 고정점이다");

            // meta를 모르는 재질은 legacy 표기로 폴백한다 — 조용한 소실 금지.
            Material metaless;
            metaless.m_name = "MetalessProbe";
            YAML::Node fallback = DataSystems->SerializeMaterialPayload(metaless);
            check.Check(!fallback["schema"] && fallback["m_name"],
                "meta 없는 재질은 legacy 표기로 폴백한다");

            // MeshRenderer embed — OnAfterSerialize가 m_Material 서브트리를
            // 정본 writer 출력으로 교체한다(씬·프리팹 저장의 실제 경로).
            MeshRenderer writerRenderer;
            {
                experiment::Material authoredForEmbed;
                std::string error;
                const ShaderMetaHandle handle =
                    DataSystems->LoadShaderMetaHandle(fixtureGuid, error);
                const std::shared_ptr<const ShaderMeta> fixtureMeta =
                    DataSystems->ResolveShaderMeta(handle);
                auto owned = std::make_shared<Material>();
                if (fixtureMeta
                    && experiment::DeserializeMaterialAuthoring(authoredNode,
                        authoredForEmbed, error)
                    && ExperimentMaterialMigration::ConvertToLegacyMaterial(
                        authoredForEmbed, fixtureMeta.get(), *owned, error))
                {
                    DataSystems->FinalizeMaterialRuntime(*owned);
                    writerRenderer.m_Material = std::move(owned);
                }
            }
            check.Check(nullptr != writerRenderer.m_Material,
                "writer 검사용 재질 준비");
            YAML::Node componentNode = Meta::Serialize(&writerRenderer);
            const YAML::Node embedded = componentNode["m_Material"];
            check.Check(embedded && embedded.IsMap() && embedded["schema"]
                && embedded["shaderAssetId"],
                "컴포넌트 직렬화의 m_Material embed가 새 정본이다");

            // 실자산 관측 — 씬 코퍼스는 shadermeta 재질 embed가 0이라 writer
            // 전환을 원리적으로 못 본다(전부 legacy 폴백이어도 28/28 초록).
            // corpus probe의 stable=yes도 형식을 안 본다. 실물 .asset이
            // canonical로 적히는지는 여기서만 잰다.
            const file::path corpusPath =
                PathFinder::Relative("Materials\\") / "ForwardWater.asset";
            Material corpusMaterial;
            bool corpusCanonical = false;
            if (std::filesystem::is_regular_file(corpusPath))
            {
                const YAML::Node corpusNode =
                    YAML::LoadFile(corpusPath.string());
                // legacy 문서 decode — typed reflection 직결(창구 시그니처 불변).
                Meta::Deserialize(&corpusMaterial, corpusNode);
                DataSystems->FinalizeMaterialRuntime(corpusMaterial);
                const YAML::Node corpusWritten =
                    DataSystems->SerializeMaterialPayload(corpusMaterial);
                corpusCanonical = corpusWritten["schema"]
                    && corpusWritten["shaderAssetId"];
            }
            check.Check(corpusCanonical,
                "실자산(ForwardWater) 저장이 새 정본으로 나간다");
        }

        char summary[160]{};
        std::snprintf(summary, sizeof(summary),
            "  실사 단정 %zu/%zu\n", check.passed,
            check.passed + check.failed);
        outLog += summary;
        return check.failed == 0u;
    }
}
