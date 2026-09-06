#include "ExperimentParity/ExperimentMaterialSealSelfTest.h"

#include "Experiment/MaterialResolver.h"
#include "Material.h"
#include "Render/Scene/ExperimentMaterialSealing.h"
#include "ShaderMeta.h"
#include "ShaderMetaReflection.h"
#include "Texture.h"

#include <array>
#include <cstdio>
#include <cstring>
#include <limits>
#include "Experiment/Import/SceneToModelDraft.h"
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

        struct SealContract final
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

        [[nodiscard]] SealContract MakeContract(const FileGuid& defaultAlbedo)
        {
            SealContract contract;
            auto& meta = contract.meta;
            meta.name = "SealProbe";
            meta.properties = {
                { "baseColor", "Base Color", ShaderPropertyType::Float4,
                  std::array<float, 4>{ 1.0f, 1.0f, 1.0f, 1.0f } },
                { "metallic", "Metallic", ShaderPropertyType::Float, 0.0f },
                { "roughness", "Roughness", ShaderPropertyType::Float, 1.0f },
                { "albedoMap", "Albedo Map", ShaderPropertyType::Texture2D,
                  defaultAlbedo },
            };
            meta.keywords = {
                { "QUALITY", { "low", "high" } },
                { "FOG", { "off", "on" } },
            };

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

        // legacy 정본과 새 경로의 bytes를 대조한다. 새 경로가 진짜 정본을
        // 재현하는지가 브리지 패리티의 전부다.
        void CheckSealParity(Checker& check, const SealContract& contract,
            const Material& legacy, const std::string& what,
            std::vector<EnhancedMaterialTextureBinding>* outBindings = nullptr)
        {
            std::vector<std::uint8_t> legacyBytes;
            std::string error;
            check.Check(legacy.BuildShaderPropertyBlock(contract.meta,
                contract.layout, legacyBytes, error),
                what + " — legacy bytes (" + error + ")");

            ExperimentMaterialSealing::SealSource sealSource;
            check.Check(ExperimentMaterialSealing::BuildSealSourceFromLegacy(
                legacy, contract.meta, sealSource, error),
                what + " — 브리지 변환 (" + error + ")");

            std::vector<std::uint8_t> sealedBytes;
            std::vector<EnhancedMaterialTextureBinding> bindings;
            check.Check(ExperimentMaterialSealing::SealCore(sealSource,
                contract.meta, contract.layout, sealedBytes, bindings, error),
                what + " — SealCore (" + error + ")");

            check.Check(!legacyBytes.empty() && legacyBytes == sealedBytes,
                what + " — propertyBytes 비트 단위 패리티");
            if (outBindings) *outBindings = std::move(bindings);
        }
    }

    bool RunExperimentMaterialSealSelfTest(std::string& outLog)
    {
        Checker check{ outLog };
        outLog += "[experiment.matseal] 브리지 패리티 검사\n";

        const FileGuid defaultAlbedo = FileGuid::CreateRandomV4();
        const FileGuid authoredAlbedo = FileGuid::CreateRandomV4();
        const SealContract contract = MakeContract(defaultAlbedo);

        // ── 1. 논리 값 저작 재질 + texture owner ─────────────────────────
        {
            Material legacy;
            legacy.m_name = "AuthoredSealProbe";
            {
                MaterialPropertyValue baseColor;
                baseColor.m_name = "baseColor";
                baseColor.m_numericValue = { 0.5f, 0.25f, 0.125f, 1.0f };
                MaterialPropertyValue metallic;
                metallic.m_name = "metallic";
                metallic.m_numericValue = { 0.75f };
                MaterialPropertyValue albedo;
                albedo.m_name = "albedoMap";
                albedo.m_textureGuid = authoredAlbedo;
                albedo.m_textureUvSet = 1; albedo.m_textureUvOffset = {.25f, -.5f};
                albedo.m_textureUvScale = {-2.f, .75f}; albedo.m_textureUvRotation = .4f;
                legacy.m_propertyValues = { baseColor, metallic, albedo };
            }
            auto owner = std::make_shared<Texture>();
            legacy.UseTextureMap("albedoMap", owner);

            std::vector<EnhancedMaterialTextureBinding> bindings;
            CheckSealParity(check, contract, legacy, "저작 재질", &bindings);
            check.Check(bindings.size() == 1u, "texture binding 수");
            if (1u == bindings.size())
            {
                check.Check(bindings[0].propertyName == "albedoMap"
                    && bindings[0].registerIndex == 3u,
                    "binding property/register 보존");
                check.Check(bindings[0].textureOwner == owner,
                    "generation owner 보존");
                check.Check(bindings[0].coordinates == assets::TextureCoordinates{1, {.25f, -.5f}, {-2.f, .75f}, .4f}, "UV metadata sealing");
                check.Check(bindings[0].textureGuid == authoredAlbedo,
                    "저작 texture GUID 보존");
            }
        }

        // ── 2. 폴백만 있는 재질 — 픽셀 게이트의 사각을 직접 잰다 ──────────
        {
            Material legacy;
            legacy.m_name = "FallbackSealProbe";
            legacy.m_materialInfo.m_baseColor = { 0.2f, 0.3f, 0.4f, 0.6f };
            legacy.m_materialInfo.m_metallic = 0.7f;
            legacy.m_materialInfo.m_roughness = 0.25f;

            std::vector<EnhancedMaterialTextureBinding> bindings;
            CheckSealParity(check, contract, legacy, "MaterialInfo 폴백 재질",
                &bindings);
            // 논리 값이 없는 texture property는 meta 기본 GUID를 나른다.
            check.Check(1u == bindings.size()
                && bindings[0].textureGuid == defaultAlbedo,
                "meta 기본 texture GUID 폴백");
        }

        // ── 3. 기본 생성 재질 — GBuffer defaultMaterial 형태 ──────────────
        {
            Material legacy;
            CheckSealParity(check, contract, legacy, "기본 생성 재질");
        }

        // ── 4. keyword — legacy 인덱스 경로와 같은 선택 ───────────────────
        {
            Material legacy;
            legacy.m_keywordSelections = { 1 };
            ExperimentMaterialSealing::SealSource sealSource;
            std::string error;
            check.Check(ExperimentMaterialSealing::BuildSealSourceFromLegacy(
                legacy, contract.meta, sealSource, error),
                "keyword 브리지 변환");
            std::vector<std::uint16_t> selections;
            check.Check(experiment::NormalizeMaterialKeywordSelections(
                sealSource.material, contract.meta.keywords, selections, error),
                "keyword 정규화 (" + error + ")");
            check.Check(selections == std::vector<std::uint16_t>{ 1, 0 },
                "legacy 인덱스 선택이 그대로 정규화된다");
        }


        // W4: coverage is sealed from reflected owned bytes, independent of
        // legacy scalar mirrors and the material constant-buffer sentinel.
        {
            ExperimentMaterialSealing::SealSource source;
            source.material.blendMode = experiment::MaterialBlendMode::Masked;
            source.material.properties = {{"doubleSided", true}};
            auto layout = contract.layout;
            layout.properties.push_back(CbBinding("alphaCutoff", ShaderPropertyType::Float, 24, 4));
            std::vector<uint8_t> bytes(32);
            float alpha = .25f, cutoff = .125f;
            std::memcpy(bytes.data() + 12, &alpha, 4);
            std::memcpy(bytes.data() + 24, &cutoff, 4);
            EnhancedMaterialCoverage coverage;
            std::string error;
            const auto seal = [&] { error.clear(); return ExperimentMaterialSealing::SealCoverage(
                source, layout, bytes, coverage, error); };
            check.Check(seal() && coverage.flags == 11 && coverage.cutoff == cutoff
                && coverage.baseAlpha == alpha, "W4 reflected MASK/alpha/doubleSided seal");
            source.material.properties[0].value = false;
            check.Check(seal() && coverage.flags == 3, "W4 doubleSided false override");
            source.material.properties[0].value = 1.f;
            check.Check(!seal(), "W4 non-bool doubleSided rejected");
            source.material.properties[0].value = true;
            source.material.properties.push_back(source.material.properties[0]);
            check.Check(!seal(), "W4 duplicate coverage property rejected");
            source.material.properties.pop_back();
            cutoff = std::numeric_limits<float>::quiet_NaN();
            std::memcpy(bytes.data() + 24, &cutoff, 4);
            check.Check(!seal(), "W4 nonfinite cutoff rejected");
            cutoff = 1.1f; std::memcpy(bytes.data() + 24, &cutoff, 4);
            check.Check(!seal(), "W4 out-of-range cutoff rejected");
            bytes.resize(16);
            check.Check(!seal(), "W4 truncated material bytes rejected");
            source.material.blendMode = experiment::MaterialBlendMode::Opaque;
            check.Check(seal() && coverage.flags == 9, "W4 OPAQUE ignores cutoff");
            source.material.blendMode = experiment::MaterialBlendMode::Transparent;
            check.Check(seal() && coverage.flags == 13, "W4 BLEND policy");
            source.material.blendMode = static_cast<experiment::MaterialBlendMode>(255);
            check.Check(!seal(), "W4 invalid alpha mode rejected");
        }
        {
            experiment::importer::ImportedScene scene;
            scene.nodes.emplace_back();
            scene.materials.resize(3);
            scene.materials[0].alphaMode = experiment::importer::AlphaMode::Mask;
            scene.materials[0].alphaCutoff = .375f;
            scene.materials[0].emissiveStrength = 8.f;
            scene.materials[0].doubleSided = true;
            scene.materials[1].alphaMode = experiment::importer::AlphaMode::Blend;
            const auto converted = experiment::importer::ConvertToModelDraft(scene, {});
            const bool ok = converted.draft && converted.draft->materials.size() == 3;
            check.Check(ok, "W4 source material conversion");
            if (ok)
            {
                const auto& materials = converted.draft->materials;
                check.Check(materials[0].blendMode == experiment::MaterialBlendMode::Masked
                    && materials[1].blendMode == experiment::MaterialBlendMode::Transparent
                    && materials[2].blendMode == experiment::MaterialBlendMode::Opaque,
                    "W4 importer preserves all alpha modes");
                bool cutoff = false, sided = false;
                for (const auto& property : materials[0].properties)
                {
                    if (property.name == "alphaCutoff") cutoff = std::get<float>(property.value) == .375f;
                    if (property.name == "doubleSided") sided = std::get<bool>(property.value);
                }
                check.Check(cutoff && sided, "W4 importer preserves cutoff and doubleSided");
                bool strength = false;
                for (const auto& property : materials[0].properties)
                    if (property.name == "emissiveStrength") strength = std::get<float>(property.value) == 8.f;
                check.Check(strength, "W6 importer preserves HDR emissive strength");
            }
        }

        char summary[160]{};
        std::snprintf(summary, sizeof(summary),
            "  브리지 단정 %zu/%zu\n", check.passed,
            check.passed + check.failed);
        outLog += summary;
        return check.failed == 0u;
    }
}
