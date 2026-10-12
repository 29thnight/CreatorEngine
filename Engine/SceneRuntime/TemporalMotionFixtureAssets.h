#pragma once

#include "../RenderEngine/AssetDepot/AssetLink.h"
#include "../RenderEngine/Experiment/Cooked/CookedAssetCatalog.h"

#include <filesystem>
#include <string>
#include <vector>

struct TemporalMotionFixtureAssetOptions final
{
    std::string targetPlatform;
    std::string targetAbi;
    std::filesystem::path shaderDirectory;
    std::filesystem::path cacheDirectory;
};

struct TemporalMotionFixtureAssets final
{
    own::shared_owner<const experiment::cooked::ArtifactByteSource> source;
    std::vector<std::byte> manifest;
    // 경로/크기/해시는 게시할 바로 그 bytes의 기록이다. 원본은 source의 Size/ReadAt으로 읽는다.
    std::vector<experiment::cooked::AssetBlobRecord> artifacts;
    experiment::cooked::AssetSetMountOptions mountOptions;
    AssetDepot::AssetLink<assets::ModelAnimationDescriptor> model;
    AssetDepot::AssetLink<assets::ModelMeshDescriptor> staticMesh;
    AssetDepot::AssetLink<assets::ModelMeshDescriptor> skinnedMesh;
    AssetDepot::AssetLink<assets::ModelMeshDescriptor> meshletMesh;
    AssetDepot::AssetLink<assets::ModelSkeletonPayload> skeleton;
    AssetDepot::AssetLink<assets::ModelAnimationPayload> clip;
    AssetDepot::AssetLink<Material> opaqueMaterial;
    AssetDepot::AssetLink<Material> alphaMaterial;
    AssetDepot::AssetLink<Material> receiverMaterial;
    AssetDepot::AssetLink<Texture> opaqueTexture;
    AssetDepot::AssetLink<Texture> alphaTexture;

    // 실제 Scene 셰이더를 양쪽 백엔드로 준비하므로 작업 스레드에서 호출한다.
    // 입력은 호출자가 소유하며 DataSystem/Scene에는 접근하지 않는다. 실패 시 out은 유지한다.
    // authoring 자산은 게시하지 않고 임시 compiler 파일만 cacheDirectory 아래에 둔다.
    [[nodiscard]] static bool Build(const TemporalMotionFixtureAssetOptions& options,
        TemporalMotionFixtureAssets& out, std::string& failure);
};
