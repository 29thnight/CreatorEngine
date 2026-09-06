#include "ExperimentParity/ExperimentMaterialMigrateSelfTest.h"

#include "AuthoringNodeViewAccess.h" // D3-a-4
#include "AuthoringParsedDocument.h"
#include "AuthoringWriteNode.h"
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

#include <algorithm>
#include <array>
#include <cstdio>
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
            Authoring::WriteDocument document;
            if (!experiment::SerializeMaterialAuthoring(
                converted, document.Root(), outError))
            {
                return false;
            }
            Authoring::ParsedDocument reloaded =
                Authoring::ParsedDocument::ParseText(
                    document.Dump(), outError);
            if (!reloaded) return false;
            experiment::Material restored;
            if (!experiment::DeserializeMaterialAuthoring(
                reloaded.Root(), restored, outError))
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
        std::string authoredParseError;
        Authoring::ParsedDocument authoredDocument =
            Authoring::ParsedDocument::ParseText(yaml, authoredParseError);
        const Authoring::ReadNode authoredNode = authoredDocument.Root();
        const bool decoded = authoredDocument
            && DataSystems->DeserializeMaterialPayload(material,
                Authoring::NodeViewAccess::Make(authoredNode));
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
            // D3-a-4: 훅 인자가 뷰가 됐다. 자가 검사도 같은 창구로 부른다.
            std::string parseError;
            Authoring::ParsedDocument componentDocument =
                Authoring::ParsedDocument::ParseText(componentYaml, parseError);
            const Authoring::ReadNode componentNode = componentDocument.Root();
            if (componentDocument)
                renderer.OnDeserialized(
                    Authoring::NodeViewAccess::Make(componentNode));
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
            Authoring::ParsedDocument legacyDocument =
                Authoring::ParsedDocument::ParseText(
                    "m_Material:\n  m_name: TypedFilled\n", parseError);
            const Authoring::ReadNode legacyNode = legacyDocument.Root();
            if (legacyDocument)
                legacyRenderer.OnDeserialized(
                    Authoring::NodeViewAccess::Make(legacyNode));
            check.Check(legacyRenderer.m_Material == before
                && legacyRenderer.m_Material->m_name == "TypedFilled",
                "legacy 노드는 typed 인스턴스를 보존한다");
        }

        // legacy 문서는 기존 경로 그대로다 — 이중화가 legacy를 깨지 않는다.
        {
            Material legacyDocument;
            // D3-a-5b: 임시 노드로 뷰를 만들 수 없다(Make(Node&&) = delete) — 이름을 준다.
            std::string parseError;
            Authoring::ParsedDocument legacySource =
                Authoring::ParsedDocument::ParseText(
                    "m_name: LegacyProbe\n"
                    "m_shaderMetaGuid: " + fixtureGuid.ToString() + "\n"
                    "m_propertyValues:\n"
                    "  - m_name: roughness\n"
                    "    m_numericValue: [0.5]\n", parseError);
            const Authoring::ReadNode legacyDocumentNode = legacySource.Root();
            const bool legacyDecoded = legacySource
                && DataSystems->DeserializeMaterialPayload(
                    legacyDocument,
                    Authoring::NodeViewAccess::Make(legacyDocumentNode));
            check.Check(legacyDecoded
                && legacyDocument.m_name == "LegacyProbe",
                "legacy 문서 경로 무변경");
        }

        // ── S2b: writer 전환 — 저장이 새 정본을 적고 왕복이 고정점이다 ────
        {
            // 위에서 decode한 material은 fixture meta를 아는 재질이다.
            Authoring::WriteDocument writtenDocument;
            const bool writtenOk = DataSystems->SerializeMaterialPayload(
                material, writtenDocument.Root());
            const Authoring::ReadNode written = writtenDocument.Root().Read();
            check.Check(writtenOk && written["schema"]
                && written["shaderAssetId"],
                "writer가 새 정본(schema+shaderAssetId)을 적는다");

            bool tintSurvived = false;
            if (written["properties"] && written["properties"].IsSequence())
            {
                for (const Authoring::ReadNode property : written["properties"])
                {
                    if (property["name"]
                        && property["name"].AsStringChecked() == "tint"
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
                redecoded = experiment::DeserializeMaterialAuthoring(
                        written, authoredAgain, error)
                    && ExperimentMaterialMigration::ConvertToLegacyMaterial(
                        authoredAgain, nullptr, reloaded, error);
                if (redecoded)
                {
                    DataSystems->FinalizeMaterialRuntime(reloaded);
                }
            }
            const std::string firstText = writtenDocument.Dump();
            std::string secondText;
            if (redecoded)
            {
                Authoring::WriteDocument rewrittenDocument;
                if (DataSystems->SerializeMaterialPayload(
                    reloaded, rewrittenDocument.Root()))
                    secondText = rewrittenDocument.Dump();
            }
            check.Check(redecoded && !firstText.empty()
                && firstText == secondText,
                "저장→로드→재저장이 고정점이다");

            // meta를 모르는 재질은 legacy 표기로 폴백한다 — 조용한 소실 금지.
            Material metaless;
            metaless.m_name = "MetalessProbe";
            Authoring::WriteDocument fallbackDocument;
            const bool fallbackWritten = DataSystems->SerializeMaterialPayload(
                metaless, fallbackDocument.Root());
            const Authoring::ReadNode fallback =
                fallbackDocument.Root().Read();
            check.Check(fallbackWritten && !fallback["schema"]
                && fallback["m_name"],
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
                    && experiment::DeserializeMaterialAuthoring(
                        authoredNode,
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
            Authoring::WriteDocument componentDocument =
                Meta::SerializeDocument(&writerRenderer);
            const Authoring::ReadNode componentNode =
                componentDocument.Root().Read();
            const Authoring::ReadNode embedded = componentNode["m_Material"];
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
                std::string parseError;
                const Authoring::ParsedDocument corpusDocument =
                    Authoring::ParsedDocument::ParseFile(
                        corpusPath.string(), parseError);
                // legacy 문서 decode — typed reflection 직결(창구 시그니처 불변).
                if (corpusDocument)
                    Meta::Deserialize(&corpusMaterial, corpusDocument.Root());
                DataSystems->FinalizeMaterialRuntime(corpusMaterial);
                Authoring::WriteDocument corpusWrittenDocument;
                const bool corpusWrittenOk =
                    DataSystems->SerializeMaterialPayload(
                        corpusMaterial, corpusWrittenDocument.Root());
                const Authoring::ReadNode corpusWritten =
                    corpusWrittenDocument.Root().Read();
                corpusCanonical = corpusDocument && corpusWrittenOk
                    && corpusWritten["schema"]
                    && corpusWritten["shaderAssetId"];
            }
            check.Check(corpusCanonical,
                "실자산(ForwardWater) 저장이 새 정본으로 나간다");
        }

        // ── S2c-1: 모델 GUID 자립 — legacy 편법 이주와 저장 정본 ──────────
        {
            // postLoad는 TypeOps 창구로 부른다 — 훅 자체 시그니처(D3-a 이행
            // 중)에 묶이지 않는 안정 디스패치이고, 실제 씬 로드가 타는 그
            // 경로다.
            const Meta::Typed::TypeOps* ops = Meta::Typed::FindTypeOps(
                TypeTrait::GUIDCreator::GetTypeID<MeshRenderer>().m_ID_Data);
            check.Check(nullptr != ops && nullptr != ops->postLoad,
                "MeshRenderer TypeOps postLoad 등록");

            // MBC9: v4 model identity was retired. A v8 carrier still migrates even
            // if its generation is absent; a v4 carrier must never regain validity.
            const FileGuid modelProbe("01234567-89ab-8cde-8123-456789abcdef");
            MeshRenderer migrating;
            std::string parseError;
            Authoring::ParsedDocument carrierDocument =
                Authoring::ParsedDocument::ParseText(
                    "m_Material:\n"
                    "  m_name: ModelCarrier\n"
                    "  m_fileGuid: " + modelProbe.ToString() + "\n",
                    parseError);
            const Authoring::ReadNode carrier = carrierDocument.Root();
            if (carrierDocument) Meta::Deserialize(&migrating, carrier);
            if (ops && ops->postLoad)
            {
                ops->postLoad(&migrating, carrier);
            }
            check.Check(migrating.m_modelGuid == modelProbe,
                "v8 carrier migrates to m_modelGuid");

            MeshRenderer retired;
            Authoring::ParsedDocument retiredDocument = Authoring::ParsedDocument::ParseText(
                "m_Material:\n  m_name: RetiredCarrier\n  m_fileGuid: "
                    + FileGuid::CreateRandomV4().ToString() + "\n", parseError);
            if (retiredDocument) Meta::Deserialize(&retired, retiredDocument.Root());
            if (ops && ops->postLoad) ops->postLoad(&retired, retiredDocument.Root());
            check.Check(retiredDocument && retired.m_modelGuid == FileGuid{},
                "retired v4 carrier is not adopted as model identity");

            MeshRenderer owning;
            owning.m_modelGuid = modelProbe;
            Authoring::WriteDocument savedDocument =
                Meta::SerializeDocument(&owning);
            const Authoring::ReadNode saved = savedDocument.Root().Read();
            check.Check(saved["m_modelGuid"]
                && saved["m_modelGuid"].AsStringChecked()
                    == modelProbe.ToString(),
                "저장이 m_modelGuid를 자기 키로 적는다");
        }

        // ── S2c-2a: base 참조+diff — 자산 링크가 저장을 살아넘는다 ────────
        {
            const std::shared_ptr<Material> base =
                DataSystems->LoadMaterialShared("ForwardWater");
            check.Check(nullptr != base && FileGuid{} != base->m_fileGuid,
                "base 재질 자산 로드");
            if (base)
            {
                MeshRenderer linked;
                linked.m_Material = std::make_shared<Material>(*base);
                linked.m_materialBaseGuid = base->m_fileGuid;
                experiment::MaterialProperty edit;
                edit.name = "roughness";
                edit.value = 0.25f;
                std::string error;
                check.Check(ExperimentMaterialMigration::ApplyPropertyToLegacy(
                    *linked.m_Material, edit, error),
                    "인스턴스 편집 적용 (" + error + ")");

                Authoring::WriteDocument savedDocument =
                    Meta::SerializeDocument(&linked);
                const Authoring::ReadNode saved = savedDocument.Root().Read();
                const Authoring::ReadNode materialNode = saved["m_Material"];
                check.Check(materialNode && materialNode["ref"]
                    && materialNode["ref"].AsStringChecked()
                        == base->m_fileGuid.ToString(),
                    "저장이 base 참조를 적는다");
                std::size_t overrideCount = 0;
                bool roughnessOverride = false;
                if (materialNode && materialNode["overrides"]
                    && materialNode["overrides"].IsSequence())
                {
                    for (const Authoring::ReadNode entry : materialNode["overrides"])
                    {
                        ++overrideCount;
                        if (entry["name"]
                            && entry["name"].AsStringChecked() == "roughness")
                        {
                            roughnessOverride = true;
                        }
                    }
                }
                check.Check(1u == overrideCount && roughnessOverride,
                    "diff가 최소다 — 편집 1건만 override");

                // 로드 왕복 — 실제 씬 로드처럼 typed + postLoad(TypeOps)로.
                const Meta::Typed::TypeOps* ops = Meta::Typed::FindTypeOps(
                    TypeTrait::GUIDCreator::GetTypeID<MeshRenderer>()
                        .m_ID_Data);
                MeshRenderer reloaded;
                Meta::Deserialize(&reloaded, saved);
                if (ops && ops->postLoad)
                {
                    ops->postLoad(&reloaded, saved);
                }
                bool roughnessApplied = false;
                if (reloaded.m_Material)
                {
                    const auto found = std::find_if(
                        reloaded.m_Material->m_propertyValues.begin(),
                        reloaded.m_Material->m_propertyValues.end(),
                        [](const MaterialPropertyValue& value)
                        {
                            return value.m_name == "roughness";
                        });
                    roughnessApplied = found
                        != reloaded.m_Material->m_propertyValues.end()
                        && found->m_numericValue == std::vector<float>{ 0.25f };
                }
                check.Check(nullptr != reloaded.m_Material && roughnessApplied
                    && reloaded.m_materialBaseGuid == base->m_fileGuid,
                    "로드가 base+override를 실체화하고 링크를 복원한다");

                Authoring::WriteDocument resavedDocument =
                    Meta::SerializeDocument(&reloaded);
                const Authoring::ReadNode resaved =
                    resavedDocument.Root().Read();
                check.Check(resaved["m_Material"]
                    && resaved["m_Material"].Dump() == materialNode.Dump(),
                    "참조 표기 재저장 고정점");

                MeshRenderer clean;
                clean.m_Material = std::make_shared<Material>(*base);
                clean.m_materialBaseGuid = base->m_fileGuid;
                Authoring::WriteDocument cleanDocument =
                    Meta::SerializeDocument(&clean);
                const Authoring::ReadNode cleanSaved =
                    cleanDocument.Root().Read();
                check.Check(cleanSaved["m_Material"]
                    && cleanSaved["m_Material"]["ref"]
                    && !cleanSaved["m_Material"]["overrides"],
                    "무편집 링크는 diff 0");
            }
        }

        char summary[160]{};
        std::snprintf(summary, sizeof(summary),
            "  실사 단정 %zu/%zu\n", check.passed,
            check.passed + check.failed);
        outLog += summary;
        return check.failed == 0u;
    }
}
