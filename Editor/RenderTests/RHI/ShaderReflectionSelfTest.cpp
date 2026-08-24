#include "ShaderReflectionSelfTest.h"

#include "DataSystem.h"
#include "RHI/RHIShaderCompiler.h"
#include "RHI/RHIShaderSource.h"
#include "Material.h"
#include "ReflectionYml.h"
#include "ShaderMetaReflection.h"
#include "ShaderPermutationDomain.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <sstream>
#include <vector>

namespace
{
    struct ReflectionStageRequest
    {
        std::string entry;
        const char* profile{};
    };
}

bool RenderTest::RunShaderReflectionSelfTest(std::string& outLog)
{
    const std::filesystem::path metaPath = RHIShaderSource::Resolve(
        "SelfTest/ShaderMetaFixture.shadermeta");
    const FileGuid guid = DataSystems->GetFileGuid(metaPath);
    ShaderMeta meta;
    std::string error;
    if (FileGuid{} == guid || !DataSystems->LoadShaderMetaGUID(guid, meta, error))
    {
        outLog += "[shader reflection] ShaderMeta load 실패: " + error + "\n";
        return false;
    }

    std::error_code pathError;
    const std::filesystem::path sourcePath = meta.ResolveSource(metaPath);
    const std::filesystem::path shaderRoot = RHIShaderSource::Resolve("");
    const std::filesystem::path relativeSource = std::filesystem::relative(
        sourcePath, shaderRoot, pathError);
    if (pathError || relativeSource.empty())
    {
        outLog += "[shader reflection] source 상대 경로 계산 실패\n";
        return false;
    }

    const std::array<std::uint16_t, 1> keywordSelection{ 1 };
    ShaderMetaPermutation permutation;
    if (!ShaderPermutationDomain::Resolve(meta, 0, keywordSelection,
        permutation, error))
    {
        outLog += "[shader reflection] permutation resolve 실패: " + error + "\n";
        return false;
    }

    std::vector<ReflectionStageRequest> requests;
    const ShaderPassDesc& pass = meta.passes[0];
    if (pass.vertex) requests.push_back({ pass.vertex->entry, "vs_5_0" });
    if (pass.pixel) requests.push_back({ pass.pixel->entry, "ps_5_0" });
    if (pass.compute) requests.push_back({ pass.compute->entry, "cs_5_0" });

    std::vector<RHIShaderReflection> dxilStages;
    std::vector<RHIShaderReflection> spirvStages;
    dxilStages.reserve(requests.size());
    spirvStages.reserve(requests.size());
    for (const ReflectionStageRequest& request : requests)
    {
        RHIShaderReflection dxil;
        RHIShaderReflection spirv;
        const std::string sourceName = relativeSource.generic_string();
        if (!RHIShaderCompiler::ReflectFile(sourceName, request.entry,
            request.profile, RHIShaderBinary::Dxil, permutation.defines,
            dxil, error)
            || !RHIShaderCompiler::ReflectFile(sourceName, request.entry,
                request.profile, RHIShaderBinary::SpirV, permutation.defines,
                spirv, error))
        {
            outLog += "[shader reflection] Slang target layout 추출 실패: "
                + error + "\n";
            return false;
        }
        if (!AreShaderReflectionsEquivalent(dxil, spirv, error))
        {
            outLog += "[shader reflection] DXIL/SPIR-V 불일치: " + error + "\n";
            return false;
        }
        dxilStages.push_back(std::move(dxil));
        spirvStages.push_back(std::move(spirv));
    }

    ShaderMetaBindingLayout layout;
    if (!ShaderMetaReflection::Resolve(meta, dxilStages, layout, error))
    {
        outLog += "[shader reflection] ShaderMeta layout 대조 실패: " + error + "\n";
        return false;
    }

    const bool layoutMatches = "MaterialProperties" == layout.constantBufferName
        && 2 == layout.constantBufferRegister
        && 0 == layout.constantBufferSpace
        && 32 == layout.constantBufferByteSize
        && 3 == layout.properties.size()
        && "tint" == layout.properties[0].name
        && 0 == layout.properties[0].byteOffset
        && 16 == layout.properties[0].byteSize
        && "roughness" == layout.properties[1].name
        && 16 == layout.properties[1].byteOffset
        && 4 == layout.properties[1].byteSize
        && "albedoMap" == layout.properties[2].name
        && RHIShaderResourceKind::Texture == layout.properties[2].resourceKind
        && 3 == layout.properties[2].registerIndex;
    if (!layoutMatches)
    {
        outLog += "[shader reflection] material property offset/binding 계약 불일치\n";
        return false;
    }

    Material material;
    if (!material.ConfigureShaderProperties(meta, layout, error))
    {
        outLog += "[material schema] property block 구성 실패: " + error + "\n";
        return false;
    }

    Mathf::Vector4 defaultTint{};
    float defaultRoughness{};
    const bool defaultsApplied = meta.guid == material.m_shaderMetaGuid
        && 3 == material.m_propertyValues.size()
        && 1 == material.GetKeywordSelections().size()
        && 0 == material.GetKeywordSelections()[0]
        && 32 == material.GetConstantBufferData().size()
        && material.TryGetVector("MaterialProperties", "tint", defaultTint)
        && material.TryGetFloat("MaterialProperties", "roughness", defaultRoughness)
        && 1.0f == defaultTint.x && 0.5f == defaultTint.y
        && 0.25f == defaultTint.z && 1.0f == defaultTint.w
        && 0.5f == defaultRoughness;

    const Mathf::Vector4 changedTint{ 0.125f, 0.25f, 0.5f, 1.0f };
    const FileGuid textureGuid = meta.guid;
    const bool mutationsApplied = material.TrySetVector(
        "MaterialProperties", "tint", changedTint)
        && material.TrySetFloat("MaterialProperties", "roughness", 0.75f)
        && material.TrySetTextureGuid("albedoMap", textureGuid)
        && material.TrySetKeywordSelection("QUALITY", "high")
        && !material.TrySetInt("MaterialProperties", "roughness", 1)
        && !material.TrySetFloat("MaterialProperties", "missing", 1.0f)
        && !material.TrySetKeywordSelection("QUALITY", "ultra");
    if (!defaultsApplied || !mutationsApplied)
    {
        outLog += "[material schema] default/setter/type/keyword 계약 불일치\n";
        return false;
    }

    MetaYml::Node serialized = DataSystems->SerializeMaterialPayload(material);
    Material restored;
    if (!DataSystems->DeserializeMaterialPayload(restored, serialized))
    {
        outLog += "[material schema] DataSystem YAML decode 실패\n";
        return false;
    }
    const bool legacyBufferRestored = restored.m_cbufferValues.contains(
        "MaterialProperties")
        && 32 == restored.m_cbufferValues["MaterialProperties"].size();
    if (!restored.ConfigureShaderProperties(meta, layout, error))
    {
        outLog += "[material schema] YAML 복원 뒤 runtime layout 재구성 실패: "
            + error + "\n";
        return false;
    }

    Mathf::Vector4 restoredTint{};
    float restoredRoughness{};
    FileGuid restoredTexture{};
    const bool restoredValues = meta.guid == restored.m_shaderMetaGuid
        && restored.TryGetVector("MaterialProperties", "tint", restoredTint)
        && restored.TryGetFloat("MaterialProperties", "roughness", restoredRoughness)
        && restored.TryGetTextureGuid("albedoMap", restoredTexture)
        && changedTint == restoredTint
        && 0.75f == restoredRoughness
        && textureGuid == restoredTexture
        && 1 == restored.GetKeywordSelections().size()
        && 1 == restored.GetKeywordSelections()[0];

    MetaYml::Node reserialized = DataSystems->SerializeMaterialPayload(restored);
    std::ostringstream firstYaml;
    std::ostringstream secondYaml;
    firstYaml << serialized;
    secondYaml << reserialized;

    Material copied(restored);
    float originalAfterCopyWrite{};
    const bool copyIsIndependent = copied.TrySetFloat(
        "MaterialProperties", "roughness", 0.25f)
        && restored.TryGetFloat(
            "MaterialProperties", "roughness", originalAfterCopyWrite)
        && 0.75f == originalAfterCopyWrite;

	std::ostringstream binaryOutput(std::ios::out | std::ios::binary);
	const bool binaryWritten = DataSystems->SerializeMaterialBinaryPayload(
		restored, binaryOutput);
	const std::string binaryPayload = binaryOutput.str();
	std::istringstream binaryInput(binaryPayload, std::ios::in | std::ios::binary);
	Material binaryRestored;
	const bool binaryHeaderDetected =
		DataSystems->HasVersionedMaterialBinaryPayload(binaryInput);
	const bool binaryRead = DataSystems->DeserializeMaterialBinaryPayload(
		binaryRestored, binaryInput);
	if (binaryRead && !binaryRestored.ConfigureShaderProperties(meta, layout, error))
	{
		outLog += "[material schema] binary 복원 뒤 runtime layout 재구성 실패: "
			+ error + "\n";
		return false;
	}
	float binaryRoughness{};
	FileGuid binaryTexture{};
	const bool binaryValues = binaryRead
		&& binaryRestored.TryGetFloat(
			"MaterialProperties", "roughness", binaryRoughness)
		&& binaryRestored.TryGetTextureGuid("albedoMap", binaryTexture)
		&& 0.75f == binaryRoughness && textureGuid == binaryTexture;

	std::string unsupportedPayload = binaryPayload;
	if (unsupportedPayload.size() > 5)
	{
		unsupportedPayload[4] = static_cast<char>(0x7f);
		unsupportedPayload[5] = 0;
	}
	std::istringstream unsupportedInput(
		unsupportedPayload, std::ios::in | std::ios::binary);
	Material unsupportedMaterial;
	const bool unsupportedVersionRejected =
		!DataSystems->DeserializeMaterialBinaryPayload(
			unsupportedMaterial, unsupportedInput);
	std::string truncatedPayload = binaryPayload;
	if (!truncatedPayload.empty()) truncatedPayload.pop_back();
	std::istringstream truncatedInput(
		truncatedPayload, std::ios::in | std::ios::binary);
	Material truncatedMaterial;
	const bool truncatedRejected = !DataSystems->DeserializeMaterialBinaryPayload(
		truncatedMaterial, truncatedInput);

	Material legacyTextures;
	legacyTextures.m_baseColorTexName = "Cube_Mat_BaseColor.png";
	legacyTextures.m_normalTexName = "Cube_Mat_BaseColor.png";
	legacyTextures.m_ORM_TexName = "Cube_Mat_BaseColor.png";
	legacyTextures.m_AO_TexName = "Cube_Mat_BaseColor.png";
	legacyTextures.m_EmissiveTexName = "Cube_Mat_BaseColor.png";
	DataSystems->FinalizeMaterialRuntime(legacyTextures);
	const FileGuid legacyTextureGuid =
		DataSystems->GetFilenameToGuid("Cube_Mat_BaseColor.png");
	auto hasMappedTexture = [&legacyTextures, &legacyTextureGuid](std::string_view name)
	{
		return std::ranges::any_of(legacyTextures.m_propertyValues,
			[name, &legacyTextureGuid](const MaterialPropertyValue& value)
			{
				return value.m_name == name
					&& value.m_textureGuid == legacyTextureGuid;
			});
	};
	const bool legacyTexturesMapped = legacyTextureGuid != FileGuid{}
		&& 5 == legacyTextures.m_propertyValues.size()
		&& hasMappedTexture("baseColorMap") && hasMappedTexture("normalMap")
		&& hasMappedTexture("ormMap") && hasMappedTexture("aoMap")
		&& hasMappedTexture("emissiveMap")
		&& legacyTextures.m_pBaseColor && legacyTextures.m_pNormal
		&& legacyTextures.m_pOccRoughMetal && legacyTextures.m_AOMap
		&& legacyTextures.m_pEmissive;

    if (!legacyBufferRestored || !restoredValues
        || firstYaml.str() != secondYaml.str() || !copyIsIndependent)
    {
        outLog += "[material schema] 저장-load-재저장/copy 소유권 계약 불일치\n";
        return false;
    }
	if (!binaryWritten || !binaryHeaderDetected || !binaryValues
		|| !unsupportedVersionRejected || !truncatedRejected
		|| !legacyTexturesMapped)
	{
		outLog += "[material schema] binary v1/legacy texture GUID 이행 계약 불일치\n";
		return false;
	}

    outLog += "[material schema] GUID + property 3 + keyword 1 · 32B repack · "
        "type 거부 · DataSystem YAML diff 0 · binary v1 · legacy texture GUID 5 · "
		"copy 독립 통과\n";

    ShaderMeta mismatchedMeta = meta;
    mismatchedMeta.properties[1].type = ShaderPropertyType::Float4;
    ShaderMetaBindingLayout invalidLayout;
    std::string typeError;
    const bool typeMismatchRejected = !ShaderMetaReflection::Resolve(
        mismatchedMeta, dxilStages, invalidLayout, typeError)
        && std::string::npos != typeError.find("type");

    RHIShaderReflection mismatchedTarget = spirvStages.front();
    if (mismatchedTarget.resources.empty())
    {
        outLog += "[shader reflection] target mismatch 음성 대조 resource가 없다\n";
        return false;
    }
    ++mismatchedTarget.resources.front().registerIndex;
    std::string targetError;
    const bool targetMismatchRejected = !AreShaderReflectionsEquivalent(
        dxilStages.front(), mismatchedTarget, targetError);
    if (!typeMismatchRejected || !targetMismatchRejected)
    {
        outLog += "[shader reflection] schema/target mismatch 거부 계약 불일치\n";
        return false;
    }

    outLog += "[shader reflection] Slang DXIL/SPIR-V "
        + std::to_string(requests.size())
        + " stages 동등 · b2/32B · tint@0 · roughness@16 · albedoMap@t3 통과\n";
    return true;
}
