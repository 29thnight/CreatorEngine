#include "ShaderReflectionSelfTest.h"
#include "AuthoringNodeViewAccess.h" // D3-a-5b

#include "DataSystem.h"
#include "FoliageType.h"
#include "Mesh.h"
#include "RHI/RHIShaderCompiler.h"
#include "RHI/RHIShaderSource.h"
#include "Material.h"
#include "ReflectionYml.h"
#include "ShaderMetaReflection.h"
#include "ShaderPermutationDomain.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <exception>
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

static bool RunShaderReflectionSelfTestImpl(const std::string& texturePath, std::string& outLog,
    const char*& stage)
{
	const FileGuid randomAssetGuidA = FileGuid::CreateRandomV4();
	const FileGuid randomAssetGuidB = FileGuid::CreateRandomV4();
	const bool randomAssetGuidContract = FileGuid{} != randomAssetGuidA
		&& FileGuid{} != randomAssetGuidB
		&& randomAssetGuidA != randomAssetGuidB
		&& randomAssetGuidA.IsRandomV4()
		&& FileGuid(randomAssetGuidA.ToString()) == randomAssetGuidA;
	if (!randomAssetGuidContract)
	{
		outLog += "[shader reflection] D2 random asset UUIDv4 계약 불일치\n";
		return false;
	}

	AssetMetaRegistry catalogProbe;
	const FileGuid firstCatalogGuid = FileGuid::CreateRandomV4();
	const FileGuid secondCatalogGuid = FileGuid::CreateRandomV4();
	const std::filesystem::path firstCatalogPath = "D2/First.asset";
	const std::filesystem::path secondCatalogPath = "D2/Second.asset";
	const bool catalogRegistrationClosed =
		AssetMetaRegistrationResult::Invalid ==
			catalogProbe.Register({}, firstCatalogPath)
		&& AssetMetaRegistrationResult::Invalid ==
			catalogProbe.Register(firstCatalogGuid, {})
		&& AssetMetaRegistrationResult::Registered ==
			catalogProbe.Register(firstCatalogGuid, firstCatalogPath)
		&& AssetMetaRegistrationResult::AlreadyRegistered ==
			catalogProbe.Register(firstCatalogGuid, firstCatalogPath)
		&& AssetMetaRegistrationResult::GuidConflict ==
			catalogProbe.Register(firstCatalogGuid, secondCatalogPath)
		&& AssetMetaRegistrationResult::PathConflict ==
			catalogProbe.Register(secondCatalogGuid, firstCatalogPath)
		&& firstCatalogPath == catalogProbe.GetPath(firstCatalogGuid)
		&& firstCatalogGuid == catalogProbe.GetGuid(firstCatalogPath)
		&& !catalogProbe.Contains(secondCatalogGuid)
		&& !catalogProbe.Contains(secondCatalogPath);
	if (!catalogRegistrationClosed)
	{
		outLog += "[shader reflection] D2 asset meta collision fail-closed 계약 불일치\n";
		return false;
	}

    stage = "shader meta load";
    const std::filesystem::path metaPath = RHIShaderSource::Resolve(
        "SelfTest/ShaderMetaFixture.shadermeta");
    const FileGuid guid = DataSystems->GetFileGuid(metaPath);
    std::string error;
    const ShaderMetaHandle metaHandle = DataSystems->LoadShaderMetaHandle(guid, error);
    const std::shared_ptr<const ShaderMeta> metaSnapshot =
        DataSystems->ResolveShaderMeta(metaHandle);
    const ShaderMetaHandle cachedMetaHandle =
        DataSystems->LoadShaderMetaHandle(guid, error);
    const std::shared_ptr<const ShaderMeta> cachedMetaSnapshot =
        DataSystems->ResolveShaderMeta(cachedMetaHandle);
    if (FileGuid{} == guid || !metaHandle.IsValid() || !metaSnapshot
        || cachedMetaHandle != metaHandle
        || cachedMetaSnapshot.get() != metaSnapshot.get())
    {
        outLog += "[shader reflection] ShaderMeta load 실패: " + error + "\n";
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

    stage = "DXIL and SPIR-V reflection";
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

    stage = "material schema";
    Material material;
    if (!material.ConfigureShaderProperties(meta, layout, error, metaHandle))
    {
        outLog += "[material schema] property block 구성 실패: " + error + "\n";
        return false;
    }

    math::vector4 defaultTint{};
    float defaultRoughness{};
    const bool defaultsApplied = meta.guid == material.m_shaderMetaGuid
        && material.GetShaderMetaHandle() == metaHandle
        && 3 == material.m_propertyValues.size()
        && 1 == material.GetKeywordSelections().size()
        && 0 == material.GetKeywordSelections()[0]
        && 32 == material.GetConstantBufferData().size()
        && material.TryGetVector("MaterialProperties", "tint", defaultTint)
        && material.TryGetFloat("MaterialProperties", "roughness", defaultRoughness)
        && 1.0f == defaultTint.x && 0.5f == defaultTint.y
        && 0.25f == defaultTint.z && 1.0f == defaultTint.w
        && 0.5f == defaultRoughness;

    const math::vector4 changedTint{ 0.125f, 0.25f, 0.5f, 1.0f };
    const FileGuid textureGuid = meta.guid;
    Material invalidHandleMaterial;
    std::string invalidHandleError;
    const bool invalidHandleRejected = !invalidHandleMaterial.ConfigureShaderProperties(
        meta, layout, invalidHandleError, {})
        && std::string::npos != invalidHandleError.find("handle");
    const bool mutationsApplied = material.TrySetVector(
        "MaterialProperties", "tint", changedTint)
        && material.TrySetFloat("MaterialProperties", "roughness", 0.75f)
        && material.TrySetTextureGuid("albedoMap", textureGuid)
        && material.TrySetKeywordSelection("QUALITY", "high")
        && !material.TrySetInt("MaterialProperties", "roughness", 1)
        && !material.TrySetFloat("MaterialProperties", "missing", 1.0f)
        && !material.TrySetKeywordSelection("QUALITY", "ultra")
        && invalidHandleRejected;
    if (!defaultsApplied || !mutationsApplied)
    {
        outLog += "[material schema] default/setter/type/keyword 계약 불일치\n";
        return false;
    }

    stage = "material authoring round trip";
    Authoring::WriteDocument serializedDocument;
    const bool serializedOk = DataSystems->SerializeMaterialPayload(
        material, serializedDocument.Root());
    const Authoring::ReadNode serialized =
        serializedDocument.Root().Read();
    Material restored;
    if (!serializedOk
        || !restored.ConfigureShaderProperties(meta, layout, error, metaHandle)
        || !DataSystems->DeserializeMaterialPayload(restored,
            Authoring::NodeViewAccess::Make(serialized)))
    {
        outLog += "[material schema] DataSystem YAML decode 실패\n";
        return false;
    }
    const bool legacyBufferRestored = restored.m_cbufferValues.contains(
        "MaterialProperties")
        && 32 == restored.m_cbufferValues["MaterialProperties"].size()
        && !restored.GetShaderMetaHandle().IsValid();
    if (!restored.ConfigureShaderProperties(meta, layout, error, metaHandle))
    {
        outLog += "[material schema] YAML 복원 뒤 runtime layout 재구성 실패: "
            + error + "\n";
        return false;
    }

    math::vector4 restoredTint{};
    float restoredRoughness{};
    FileGuid restoredTexture{};
    const bool restoredValues = meta.guid == restored.m_shaderMetaGuid
        && restored.GetShaderMetaHandle() == metaHandle
        && restored.TryGetVector("MaterialProperties", "tint", restoredTint)
        && restored.TryGetFloat("MaterialProperties", "roughness", restoredRoughness)
        && restored.TryGetTextureGuid("albedoMap", restoredTexture)
        && changedTint == restoredTint
        && 0.75f == restoredRoughness
        && textureGuid == restoredTexture
        && 1 == restored.GetKeywordSelections().size()
        && 1 == restored.GetKeywordSelections()[0];

    Authoring::WriteDocument reserializedDocument;
    const bool reserializedOk = DataSystems->SerializeMaterialPayload(
        restored, reserializedDocument.Root());

    Material copied(restored);
    float originalAfterCopyWrite{};
    const bool copyIsIndependent = copied.TrySetFloat(
        "MaterialProperties", "roughness", 0.25f)
        && copied.GetShaderMetaHandle() == metaHandle
        && restored.TryGetFloat(
            "MaterialProperties", "roughness", originalAfterCopyWrite)
        && 0.75f == originalAfterCopyWrite;

    stage = "material binary round trip";
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
	if (binaryRead && !binaryRestored.ConfigureShaderProperties(
		meta, layout, error, metaHandle))
	{
		outLog += "[material schema] binary 복원 뒤 runtime layout 재구성 실패: "
			+ error + "\n";
		return false;
	}
	float binaryRoughness{};
	FileGuid binaryTexture{};
	const bool binaryValues = binaryRead
		&& binaryRestored.GetShaderMetaHandle() == metaHandle
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

    stage = "texture mapping and ownership";
	Material legacyTextures;
	legacyTextures.m_baseColorTexName = file::path(texturePath).filename().string();
	legacyTextures.m_normalTexName = file::path(texturePath).filename().string();
	legacyTextures.m_ORM_TexName = file::path(texturePath).filename().string();
	legacyTextures.m_AO_TexName = file::path(texturePath).filename().string();
	legacyTextures.m_EmissiveTexName = file::path(texturePath).filename().string();
	const FileGuid legacyTextureGuid =
		DataSystems->GetFileGuid(file::path(texturePath));
	MaterialPropertyValue genericTexture;
	genericTexture.m_name = "baseMap";
	genericTexture.m_textureGuid = legacyTextureGuid;
	legacyTextures.m_propertyValues.push_back(std::move(genericTexture));
	DataSystems->FinalizeMaterialRuntime(legacyTextures);
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
		&& 6 == legacyTextures.m_propertyValues.size()
		&& hasMappedTexture("baseColorMap") && hasMappedTexture("normalMap")
		&& hasMappedTexture("ormMap") && hasMappedTexture("aoMap")
		&& hasMappedTexture("emissiveMap") && hasMappedTexture("baseMap")
		&& legacyTextures.GetBaseColorMapShared()
		&& legacyTextures.GetNormalMapShared()
		&& legacyTextures.GetOccRoughMetalMapShared()
		&& legacyTextures.GetAOMapShared()
		&& legacyTextures.GetEmissiveMapShared();
	const Material legacyTextureCopy = legacyTextures;
	const bool legacyTextureOwners = legacyTextures.GetBaseColorMapShared()
		&& legacyTextures.GetNormalMapShared()
		&& legacyTextures.GetOccRoughMetalMapShared()
		&& legacyTextures.GetAOMapShared()
		&& legacyTextures.GetEmissiveMapShared()
		&& legacyTextures.GetTextureMapShared("baseMap")
		&& 6u == legacyTextures.GetTextureOwners().size()
		&& legacyTextureCopy.GetBaseColorMapShared()
			== legacyTextures.GetBaseColorMapShared()
		&& legacyTextureCopy.GetNormalMapShared()
			== legacyTextures.GetNormalMapShared()
		&& legacyTextureCopy.GetOccRoughMetalMapShared()
			== legacyTextures.GetOccRoughMetalMapShared()
		&& legacyTextureCopy.GetAOMapShared() == legacyTextures.GetAOMapShared()
		&& legacyTextureCopy.GetEmissiveMapShared()
			== legacyTextures.GetEmissiveMapShared()
		&& legacyTextureCopy.GetTextureMapShared("baseMap")
			== legacyTextures.GetTextureMapShared("baseMap")
		&& 6u == legacyTextureCopy.GetTextureOwners().size();

    if (!legacyBufferRestored || !restoredValues
        || !reserializedOk
        || serializedDocument.Dump() != reserializedDocument.Dump()
        || !copyIsIndependent)
    {
        outLog += "[material schema] 저장-load-재저장/copy 소유권 계약 불일치\n";
        return false;
    }
	if (!binaryWritten || !binaryHeaderDetected || !binaryValues
		|| !unsupportedVersionRejected || !truncatedRejected
		|| !legacyTexturesMapped || !legacyTextureOwners)
	{
		outLog += "[material schema] binary v1/legacy texture GUID·owner 이행 계약 불일치\n";
		return false;
	}

	outLog += "[material schema] GUID + property 3 + keyword 1 · 32B repack · "
		"type 거부 · DataSystem YAML diff 0 · binary v1 · legacy texture GUID 5 · "
		"generic baseMap owner vector 6/raw alias 0·copy 공동 소유 · property copy 독립 통과\n";

    stage = "reflection negative cases";
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

    // M5-C3a: watcher thread의 게시만으로는 cache를 바꾸지 않는다. 같은 저장의
    // 중복 Modified는 하나로 합쳐지고 GT 프레임 경계 drain 뒤에만 generation을
    // 올려 이전 handle resolve를 거부해야 한다.
    stage = "shader meta reload";
    DataSystems->QueueAssetChange({ RuntimeAssetChangeKind::ContentReload,
        RuntimeAssetType::ShaderMeta, guid, metaPath });
    DataSystems->QueueAssetChange({ RuntimeAssetChangeKind::ContentReload,
        RuntimeAssetType::ShaderMeta, guid, metaPath });
    const bool queuedHandleStillValid =
        DataSystems->ResolveShaderMeta(metaHandle).get() == metaSnapshot.get();
    const std::size_t drainedAssetChanges = DataSystems->DrainQueuedAssetChanges();
    const bool staleHandleRejected =
        !DataSystems->ResolveShaderMeta(metaHandle) && metaSnapshot;
    const ShaderMetaHandle reloadedHandle =
        DataSystems->LoadShaderMetaHandle(guid, error);
    const std::shared_ptr<const ShaderMeta> reloadedMeta =
        DataSystems->ResolveShaderMeta(reloadedHandle);
    const ShaderMetaHandle reloadedCachedHandle =
        DataSystems->LoadShaderMetaHandle(guid, error);
    const std::shared_ptr<const ShaderMeta> reloadedCachedMeta =
        DataSystems->ResolveShaderMeta(reloadedCachedHandle);
    Material reloadedMaterial;
    const bool reconfigured = reloadedMeta
        && reloadedMaterial.ConfigureShaderProperties(
            *reloadedMeta, layout, error, reloadedHandle);
    const bool generationAdvanced = queuedHandleStillValid
        && drainedAssetChanges == 1
        && staleHandleRejected
        && reloadedHandle.IsValid()
        && reloadedHandle.slot == metaHandle.slot
        && reloadedHandle.generation != metaHandle.generation
        && reloadedCachedHandle == reloadedHandle
        && reloadedCachedMeta.get() == reloadedMeta.get()
        && reconfigured
        && reloadedMaterial.GetShaderMetaHandle() == reloadedHandle;
    if (!generationAdvanced)
    {
        outLog += "[shader reflection] ShaderMeta generation cache 계약 불일치: "
            + error + "\n";
        return false;
    }

    // M5-C4a(MBC9 재단): FoliageType의 runtime 재질은 공동 소유(shared_ptr)여야
    // cache generation이 바뀐 뒤 UAF가 없다. 메시는 typed generation이 소유하므로
    // 이 프로브는 재질 축만 잰다 — 원 소유자와 복사본이 사라지는 각 경계에서
    // 소유가 유지·해제되는지 단정한다.
    std::weak_ptr<Material> foliageMaterialLifetime;
    {
        auto material = std::make_shared<Material>();
        foliageMaterialLifetime = material;

    stage = "foliage ownership and serialization";
        FoliageType foliage("C4LifetimeProbe", true);
        foliage.m_material = material;
        material.reset();
        if (foliageMaterialLifetime.expired())
        {
            outLog += "[shader reflection] FoliageType owning reference 적용 실패\n";
            return false;
        }

        Authoring::WriteDocument foliageDocument =
            Meta::SerializeDocument(&foliage);
        const Authoring::ReadNode foliagePayload =
            foliageDocument.Root().Read();
        FoliageType restoredFoliage;
        Meta::Deserialize(&restoredFoliage, foliagePayload);
        if (restoredFoliage.m_modelName != foliage.m_modelName
            || restoredFoliage.m_castShadow != foliage.m_castShadow
            || restoredFoliage.m_isShadowRecive != foliage.m_isShadowRecive
            || restoredFoliage.m_material || restoredFoliage.m_modelGeneration)
        {
            outLog += "[shader reflection] FoliageType asset/runtime owner 분리 실패\n";
            return false;
        }

        FoliageType packetCopy = foliage;
        foliage = {};
        if (foliageMaterialLifetime.expired())
        {
            outLog += "[shader reflection] FoliageType packet copy 수명 보존 실패\n";
            return false;
        }
    }
    if (!foliageMaterialLifetime.expired())
    {
        outLog += "[shader reflection] FoliageType 마지막 owner 해제 실패\n";
        return false;
    }

    const bool retiredPolicyClosed =
        !DataSystem::RequiresLegacyRetiredGeneration(RuntimeAssetType::Model)
        && !DataSystem::RequiresLegacyRetiredGeneration(RuntimeAssetType::Material)
        && DataSystem::RequiresLegacyRetiredGeneration(RuntimeAssetType::Texture)
        && DataSystem::RequiresLegacyRetiredGeneration(RuntimeAssetType::UITexture)
        && DataSystem::RequiresLegacyRetiredGeneration(RuntimeAssetType::SpriteSheet);
    if (!retiredPolicyClosed)
    {
        outLog += "[shader reflection] C4 retired generation 축소 정책 불일치\n";
        return false;
    }

    // Material cache에서 이전 generation을 분리해도 Foliage owner가 있는 동안만
    // 생존하고, 마지막 실제 consumer가 사라지면 전역 retired 목록 없이 파괴된다.
    stage = "material cache lifetime";
    constexpr std::string_view lifetimeProbeName = "M5_C4_FoliageMaterialProbe";
    auto cacheMaterial = std::make_shared<Material>();
    cacheMaterial->m_name = lifetimeProbeName;
    std::weak_ptr<Material> cacheMaterialLifetime = cacheMaterial;
    FoliageType cacheConsumer("C4CacheProbe", true);
    cacheConsumer.m_material = cacheMaterial;
    DataSystems->InsertMaterial(cacheMaterial);
    const bool inserted = DataSystems->FindCachedMaterial(lifetimeProbeName).get()
        == cacheMaterial.get();
    cacheMaterial.reset();
    DataSystems->QueueAssetChange({ RuntimeAssetChangeKind::ContentReload,
        RuntimeAssetType::Material, {}, std::filesystem::path(lifetimeProbeName)
            .replace_extension(".material") });
    const std::size_t retiredDrainCount = DataSystems->DrainQueuedAssetChanges();
    const bool detached = !DataSystems->FindCachedMaterial(lifetimeProbeName);
    const bool consumerPreserved = !cacheMaterialLifetime.expired();
    cacheConsumer = {};
    const bool releasedWithLastConsumer = cacheMaterialLifetime.expired();
    if (!inserted || retiredDrainCount != 1 || !detached || !consumerPreserved
        || !releasedWithLastConsumer)
    {
        outLog += "[shader reflection] C4 Material cache/shared consumer 수명 계약 불일치\n";
        return false;
    }

    outLog += "[shader reflection] Slang DXIL/SPIR-V "
        + std::to_string(requests.size())
        + " stages 동등 · b2/32B · tint@0 · roughness@16 · albedoMap@t3 · "
        "ShaderMeta queued reload/stale handle 거부 · Foliage owning copy/asset 왕복 · "
		"D2 UUIDv4/collision fail-closed · Model/Material retired 축소 통과\n";
    return true;
}

bool RenderTest::RunShaderReflectionSelfTest(const std::string& texturePath, std::string& outLog)
{
    const char* stage = "asset identity";
    try { return RunShaderReflectionSelfTestImpl(texturePath, outLog, stage); }
    catch (const std::exception& exception)
    {
        outLog += std::string("[shader reflection] exception at ") + stage + ": " + exception.what() + "\n";
    }
    catch (...)
    {
        outLog += std::string("[shader reflection] unknown exception at ") + stage + "\n";
    }
    return false;
}
