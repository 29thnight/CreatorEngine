#include "ExperimentParity/ExperimentMaterialParitySelfTest.h"

#include "DataSystem.h"
#include "Experiment/MaterialPropertyBlock.h"
#include "Material.h"
#include "RHI/RHIShaderCompiler.h"
#include "RHI/RHIShaderSource.h"
#include "ShaderMeta.h"
#include "ShaderMetaReflection.h"
#include "ShaderPermutationDomain.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
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

        [[nodiscard]] ShaderMetaPropertyBinding TextureBinding(std::string name)
        {
            ShaderMetaPropertyBinding binding;
            binding.name = std::move(name);
            binding.propertyType = ShaderPropertyType::Texture2D;
            binding.resourceKind = RHIShaderResourceKind::Texture;
            binding.resourceName = binding.name;
            return binding;
        }

        // 타입 8종 전부를 품는 합성 계약. 이름은 일부러 표준 property 집합
        // 밖에서 고른다 — legacy MaterialInfo 폴백이 기본값 경로에 끼어들면
        // "기본값 패리티"가 다른 것을 재게 된다(표준 이름 경계는 별도 케이스).
        struct SyntheticContract final
        {
            ShaderMeta meta{};
            ShaderMetaBindingLayout layout{};
        };

        [[nodiscard]] SyntheticContract MakeSyntheticContract()
        {
            SyntheticContract contract;
            auto& meta = contract.meta;
            meta.name = "ParityProbe";
            meta.properties = {
                { "tint", "Tint", ShaderPropertyType::Float4,
                  std::array<float, 4>{ 1.0f, 0.5f, 0.25f, 1.0f } },
                { "sheen", "Sheen", ShaderPropertyType::Float, 0.5f },
                { "uvScale", "UV Scale", ShaderPropertyType::Float2,
                  std::monostate{} },
                { "glow", "Glow", ShaderPropertyType::Float3,
                  std::array<float, 3>{ 0.0f, 0.25f, 0.5f } },
                { "steps", "Steps", ShaderPropertyType::Int, std::int32_t{ 3 } },
                { "useFlag", "Use Flag", ShaderPropertyType::Bool, true },
                { "warp", "Warp", ShaderPropertyType::Float4x4,
                  std::monostate{} },
                { "albedoMap", "Albedo Map", ShaderPropertyType::Texture2D,
                  std::monostate{} },
            };

            auto& layout = contract.layout;
            layout.constantBufferName = "MaterialProperties";
            layout.constantBufferByteSize = 128;
            layout.properties = {
                CbBinding("tint", ShaderPropertyType::Float4, 0, 16),
                CbBinding("sheen", ShaderPropertyType::Float, 16, 4),
                CbBinding("uvScale", ShaderPropertyType::Float2, 20, 8),
                CbBinding("glow", ShaderPropertyType::Float3, 32, 12),
                CbBinding("steps", ShaderPropertyType::Int, 44, 4),
                CbBinding("useFlag", ShaderPropertyType::Bool, 48, 4),
                CbBinding("warp", ShaderPropertyType::Float4x4, 52, 64),
                TextureBinding("albedoMap"),
            };
            return contract;
        }

        [[nodiscard]] MaterialPropertyValue LegacyNumeric(std::string name,
            std::vector<float> values)
        {
            MaterialPropertyValue value;
            value.m_name = std::move(name);
            value.m_numericValue = std::move(values);
            return value;
        }

        [[nodiscard]] float FloatAt(const std::vector<std::uint8_t>& bytes,
            std::size_t offset)
        {
            float value{};
            std::memcpy(&value, bytes.data() + offset, sizeof(value));
            return value;
        }

        [[nodiscard]] std::int32_t IntAt(const std::vector<std::uint8_t>& bytes,
            std::size_t offset)
        {
            std::int32_t value{};
            std::memcpy(&value, bytes.data() + offset, sizeof(value));
            return value;
        }

        // 성공해야 하는 쌍을 빌드해 비트 단위로 대조한다.
        void CheckParity(Checker& check, const ShaderMeta& meta,
            const ShaderMetaBindingLayout& layout, const Material& legacy,
            const experiment::Material& probe, const std::string& what,
            std::vector<std::uint8_t>* outBytes = nullptr)
        {
            std::vector<std::uint8_t> legacyBytes;
            std::vector<std::uint8_t> experimentBytes;
            std::string legacyError;
            std::string experimentError;
            const bool legacyBuilt = legacy.BuildShaderPropertyBlock(
                meta, layout, legacyBytes, legacyError);
            const bool experimentBuilt = experiment::BuildMaterialPropertyBlock(
                probe, meta, layout, experimentBytes, experimentError);
            check.Check(legacyBuilt, what + " — legacy build (" + legacyError + ")");
            check.Check(experimentBuilt,
                what + " — experiment build (" + experimentError + ")");
            if (!legacyBuilt || !experimentBuilt) return;
            check.Check(legacyBytes.size() == experimentBytes.size(),
                what + " — 크기 동등");
            check.Check(legacyBytes == experimentBytes,
                what + " — 비트 단위 패리티");
            if (outBytes) *outBytes = std::move(experimentBytes);
        }

        void CheckExperimentRejected(Checker& check, const ShaderMeta& meta,
            const ShaderMetaBindingLayout& layout,
            const experiment::Material& probe, const std::string& what)
        {
            std::vector<std::uint8_t> bytes;
            std::string error;
            const bool built = experiment::BuildMaterialPropertyBlock(
                probe, meta, layout, bytes, error);
            check.Check(!built, what + " — 거부해야 한다");
            check.Check(!error.empty(), what + " — 거부 사유가 있어야 한다");
            check.Check(bytes.empty(), what + " — 실패 시 출력이 비어야 한다");
        }
    }

    bool RunExperimentMaterialParitySelfTest(std::string& outLog)
    {
        Checker check{ outLog };
        outLog += "[experiment.matparity] 합성 검사\n";

        const SyntheticContract contract = MakeSyntheticContract();
        const ShaderMeta& meta = contract.meta;
        const ShaderMetaBindingLayout& layout = contract.layout;

        const FileGuid textureGuid = FileGuid::CreateRandomV4();
        experiment::AssetId textureAssetId{};
        textureAssetId.value = textureGuid.m_guid;

        // ── 1. 기본값만 — 저작값 0개, 양쪽 다 ShaderMeta default 경로 ──────
        std::vector<std::uint8_t> defaultBytes;
        {
            Material legacy;
            experiment::Material probe;
            CheckParity(check, meta, layout, legacy, probe,
                "기본값만", &defaultBytes);

            // 골든 눈검산 — 패리티는 "둘 다 틀린" 경우를 원리적으로 못 가른다.
            // 기본값 몇 개는 절대값으로 못박는다.
            if (!defaultBytes.empty())
            {
                check.Check(1.0f == FloatAt(defaultBytes, 0)
                    && 0.5f == FloatAt(defaultBytes, 4)
                    && 0.25f == FloatAt(defaultBytes, 8),
                    "기본값 — tint 절대값");
                check.Check(0.5f == FloatAt(defaultBytes, 16),
                    "기본값 — sheen 절대값");
                check.Check(0.0f == FloatAt(defaultBytes, 20)
                    && 0.0f == FloatAt(defaultBytes, 24),
                    "기본값 — monostate uvScale은 0이어야 한다");
                check.Check(3 == IntAt(defaultBytes, 44),
                    "기본값 — steps 절대값");
                check.Check(1 == IntAt(defaultBytes, 48),
                    "기본값 — bool true는 4바이트 1이어야 한다");
                check.Check(0.0f == FloatAt(defaultBytes, 52),
                    "기본값 — monostate warp는 0이어야 한다");
            }
        }

        // ── 2. 전 타입 저작값 ──────────────────────────────────────────────
        {
            Material legacy;
            legacy.m_propertyValues = {
                LegacyNumeric("tint", { 0.125f, 0.25f, 0.5f, 0.875f }),
                LegacyNumeric("sheen", { 0.75f }),
                LegacyNumeric("uvScale", { 2.0f, 4.0f }),
                LegacyNumeric("glow", { 0.1f, 0.2f, 0.3f }),
            };
            {
                MaterialPropertyValue steps;
                steps.m_name = "steps";
                steps.m_integerValue = 7;
                legacy.m_propertyValues.push_back(steps);
                MaterialPropertyValue useFlag;
                useFlag.m_name = "useFlag";
                useFlag.m_boolValue = false;
                legacy.m_propertyValues.push_back(useFlag);
                MaterialPropertyValue albedo;
                albedo.m_name = "albedoMap";
                albedo.m_textureGuid = textureGuid;
                legacy.m_propertyValues.push_back(albedo);
            }

            experiment::Material probe;
            probe.properties = {
                { "tint", math::vector4{ 0.125f, 0.25f, 0.5f, 0.875f } },
                { "sheen", 0.75f },
                { "uvScale", math::vector2{ 2.0f, 4.0f } },
                { "glow", math::vector3{ 0.1f, 0.2f, 0.3f } },
                { "steps", std::int32_t{ 7 } },
                { "useFlag", false },
                { "albedoMap",
                  experiment::TextureReference{ textureAssetId, "albedo" } },
            };

            std::vector<std::uint8_t> authoredBytes;
            CheckParity(check, meta, layout, legacy, probe,
                "전 타입 저작값", &authoredBytes);
            // 값이 실제로 흘렀는가 — 둘 다 기본값으로 굴러도 패리티는 초록이다.
            check.Check(!authoredBytes.empty() && authoredBytes != defaultBytes,
                "저작값이 기본값과 다른 바이트를 만들어야 한다");
            if (!authoredBytes.empty())
            {
                check.Check(7 == IntAt(authoredBytes, 44),
                    "저작값 — steps 절대값");
                check.Check(0 == IntAt(authoredBytes, 48),
                    "저작값 — bool false는 4바이트 0이어야 한다");
            }
        }

        // ── 3. 부분 저작 — 저작·기본이 한 버퍼에 섞인다 ────────────────────
        {
            Material legacy;
            legacy.m_propertyValues = { LegacyNumeric("sheen", { 0.125f }) };
            experiment::Material probe;
            probe.properties = { { "sheen", 0.125f } };
            CheckParity(check, meta, layout, legacy, probe, "부분 저작");
        }

        // ── 4. 표준 이름 경계 — MaterialInfo 폴백은 legacy 전용이다 ────────
        {
            ShaderMeta standardMeta;
            standardMeta.name = "StandardBoundary";
            standardMeta.properties = {
                { "baseColor", "Base Color", ShaderPropertyType::Float4,
                  std::array<float, 4>{ 1.0f, 1.0f, 1.0f, 1.0f } },
                { "metallic", "Metallic", ShaderPropertyType::Float, 0.0f },
            };
            ShaderMetaBindingLayout standardLayout;
            standardLayout.constantBufferName = "MaterialProperties";
            standardLayout.constantBufferByteSize = 32;
            standardLayout.properties = {
                CbBinding("baseColor", ShaderPropertyType::Float4, 0, 16),
                CbBinding("metallic", ShaderPropertyType::Float, 16, 4),
            };

            // 4-a. 저작값이 있으면 표준 이름이어도 폴백이 안 끼고 패리티가 선다.
            {
                Material legacy;
                legacy.m_materialInfo.m_baseColor = { 0.9f, 0.8f, 0.7f, 0.6f };
                legacy.m_materialInfo.m_metallic = 0.9f;
                legacy.m_propertyValues = {
                    LegacyNumeric("baseColor", { 0.2f, 0.3f, 0.4f, 1.0f }),
                    LegacyNumeric("metallic", { 0.5f }),
                };
                experiment::Material probe;
                probe.properties = {
                    { "baseColor", math::vector4{ 0.2f, 0.3f, 0.4f, 1.0f } },
                    { "metallic", 0.5f },
                };
                CheckParity(check, standardMeta, standardLayout, legacy, probe,
                    "표준 이름 + 저작값");
            }

            // 4-b. 저작값이 없으면 legacy는 MaterialInfo를, experiment는
            //      ShaderMeta 기본값을 쓴다 — 바이트가 **달라야** 하고, 그
            //      다름이 계약이다(experiment는 폴백을 승계하지 않는다).
            {
                Material legacy;
                legacy.m_materialInfo.m_baseColor = { 0.9f, 0.8f, 0.7f, 0.6f };
                legacy.m_materialInfo.m_metallic = 0.9f;
                experiment::Material probe;

                std::vector<std::uint8_t> legacyBytes;
                std::vector<std::uint8_t> experimentBytes;
                std::string error;
                check.Check(legacy.BuildShaderPropertyBlock(standardMeta,
                    standardLayout, legacyBytes, error),
                    "표준 이름 폴백 — legacy build");
                check.Check(experiment::BuildMaterialPropertyBlock(probe,
                    standardMeta, standardLayout, experimentBytes, error),
                    "표준 이름 폴백 — experiment build");
                check.Check(!legacyBytes.empty() && !experimentBytes.empty()
                    && legacyBytes != experimentBytes,
                    "저작값 부재 시 legacy MaterialInfo 폴백과 experiment 기본값은"
                    " 달라야 한다");
                check.Check(!experimentBytes.empty()
                    && 1.0f == FloatAt(experimentBytes, 0)
                    && 0.0f == FloatAt(experimentBytes, 16),
                    "experiment는 ShaderMeta 기본값을 써야 한다");
                check.Check(!legacyBytes.empty()
                    && 0.9f == FloatAt(legacyBytes, 0)
                    && 0.9f == FloatAt(legacyBytes, 16),
                    "legacy는 MaterialInfo 폴백을 써야 한다");
            }
        }

        // ── 5. fail-closed — 데이터 모델 격차는 침묵하지 않는다 ────────────
        {
            {
                experiment::Material probe;
                probe.properties = { { "sheen", std::string{ "0.5" } } };
                CheckExperimentRejected(check, meta, layout, probe,
                    "float 자리의 문자열");
            }
            {
                experiment::Material probe;
                probe.properties = { { "steps", std::uint32_t{ 7 } } };
                CheckExperimentRejected(check, meta, layout, probe,
                    "int 자리의 uint32 — 변환을 지어내지 않는다");
            }
            {
                experiment::Material probe;
                probe.properties = { { "tint", math::vector3{ 1.0f, 1.0f, 1.0f } } };
                CheckExperimentRejected(check, meta, layout, probe,
                    "float4 자리의 vector3");
            }
            {
                experiment::Material probe;
                probe.properties = { { "warp", 1.0f } };
                CheckExperimentRejected(check, meta, layout, probe,
                    "Float4x4 저작값 — 데이터 모델에 표현이 없다");
            }
            {
                experiment::Material probe;
                probe.properties = { { "albedoMap", std::string{ "albedo.png" } } };
                CheckExperimentRejected(check, meta, layout, probe,
                    "texture 자리의 문자열");
            }
            {
                ShaderMetaBindingLayout broken = layout;
                broken.properties.erase(broken.properties.begin());
                experiment::Material probe;
                CheckExperimentRejected(check, meta, broken, probe,
                    "meta property에 binding이 없다");
            }
            {
                ShaderMetaBindingLayout broken = layout;
                broken.constantBufferName.clear();
                experiment::Material probe;
                CheckExperimentRejected(check, meta, broken, probe,
                    "constant buffer 이름이 빈 layout");
            }
        }

        char summary[160]{};
        std::snprintf(summary, sizeof(summary),
            "  합성 단정 %zu/%zu\n", check.passed,
            check.passed + check.failed);
        outLog += summary;
        return check.failed == 0u;
    }

    bool RunExperimentMaterialParityReal(std::string& outLog)
    {
        Checker check{ outLog };
        outLog += "[experiment.matparity] 실사 검사 — Slang reflection layout\n";

        const std::filesystem::path metaPath = RHIShaderSource::Resolve(
            "SelfTest/ShaderMetaFixture.shadermeta");
        const FileGuid guid = DataSystems->GetFileGuid(metaPath);
        std::string error;
        const ShaderMetaHandle metaHandle =
            DataSystems->LoadShaderMetaHandle(guid, error);
        const std::shared_ptr<const ShaderMeta> metaSnapshot =
            DataSystems->ResolveShaderMeta(metaHandle);
        if (FileGuid{} == guid || !metaHandle.IsValid() || !metaSnapshot)
        {
            outLog += "    [실패] ShaderMeta load: " + error + "\n";
            return false;
        }
        const ShaderMeta meta = *metaSnapshot;

        std::error_code pathError;
        const std::filesystem::path sourcePath = meta.ResolveSource(metaPath);
        const std::filesystem::path shaderRoot = RHIShaderSource::Resolve("");
        const std::filesystem::path relativeSource = std::filesystem::relative(
            sourcePath, shaderRoot, pathError);
        if (pathError || relativeSource.empty())
        {
            outLog += "    [실패] source 상대 경로 계산\n";
            return false;
        }

        const std::array<std::uint16_t, 1> keywordSelection{ 0 };
        ShaderMetaPermutation permutation;
        if (!ShaderPermutationDomain::Resolve(meta, 0, keywordSelection,
            permutation, error))
        {
            outLog += "    [실패] permutation resolve: " + error + "\n";
            return false;
        }

        std::vector<RHIShaderReflection> stages;
        const ShaderPassDesc& pass = meta.passes[0];
        struct StageRequest final
        {
            std::string entry;
            const char* profile{};
        };
        std::vector<StageRequest> requests;
        if (pass.vertex) requests.push_back({ pass.vertex->entry, "vs_5_0" });
        if (pass.pixel) requests.push_back({ pass.pixel->entry, "ps_5_0" });
        for (const StageRequest& request : requests)
        {
            RHIShaderReflection stage;
            if (!RHIShaderCompiler::ReflectFile(relativeSource.generic_string(),
                request.entry, request.profile, RHIShaderBinary::Dxil,
                permutation.defines, stage, error))
            {
                outLog += "    [실패] Slang reflection: " + error + "\n";
                return false;
            }
            stages.push_back(std::move(stage));
        }

        ShaderMetaBindingLayout layout;
        if (!ShaderMetaReflection::Resolve(meta, stages, layout, error))
        {
            outLog += "    [실패] layout resolve: " + error + "\n";
            return false;
        }

        const FileGuid textureGuid = FileGuid::CreateRandomV4();
        experiment::AssetId textureAssetId{};
        textureAssetId.value = textureGuid.m_guid;

        // 저작값 케이스. "roughness"는 표준 이름이라 저작값 부재 시 legacy가
        // MaterialInfo 폴백으로 새므로, 실사 leg에서는 항상 저작한다.
        {
            Material legacy;
            legacy.m_propertyValues = {
                LegacyNumeric("tint", { 0.125f, 0.25f, 0.5f, 1.0f }),
                LegacyNumeric("roughness", { 0.75f }),
            };
            MaterialPropertyValue albedo;
            albedo.m_name = "albedoMap";
            albedo.m_textureGuid = textureGuid;
            legacy.m_propertyValues.push_back(albedo);

            experiment::Material probe;
            probe.properties = {
                { "tint", math::vector4{ 0.125f, 0.25f, 0.5f, 1.0f } },
                { "roughness", 0.75f },
                { "albedoMap",
                  experiment::TextureReference{ textureAssetId, "albedo" } },
            };
            CheckParity(check, meta, layout, legacy, probe,
                "실사 layout + 저작값");
        }

        // 기본값 케이스 — tint/albedoMap은 기본값 경로(비표준 이름), roughness만
        // 저작해 폴백을 배제한다.
        {
            Material legacy;
            legacy.m_propertyValues = { LegacyNumeric("roughness", { 0.5f }) };
            experiment::Material probe;
            probe.properties = { { "roughness", 0.5f } };
            std::vector<std::uint8_t> bytes;
            CheckParity(check, meta, layout, legacy, probe,
                "실사 layout + 기본값", &bytes);
            // ShaderMetaFixture 기본값 골든 눈검산 — tint [1.0, 0.5, 0.25, 1.0]
            // @0, roughness @16 (ShaderReflectionSelfTest가 못박은 오프셋).
            check.Check(!bytes.empty()
                && 1.0f == FloatAt(bytes, 0)
                && 0.5f == FloatAt(bytes, 4)
                && 0.25f == FloatAt(bytes, 8)
                && 0.5f == FloatAt(bytes, 16),
                "실사 기본값 골든 — tint/roughness 절대값");
        }

        char summary[160]{};
        std::snprintf(summary, sizeof(summary),
            "  실사 단정 %zu/%zu · CB %u바이트 · property %zu\n",
            check.passed, check.passed + check.failed,
            layout.constantBufferByteSize, layout.properties.size());
        outLog += summary;
        return check.failed == 0u;
    }
}
