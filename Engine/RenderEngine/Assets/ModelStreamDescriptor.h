#pragma once
#include "ModelMeshDescriptor.h"

namespace assets
{
// Shared validated binding for AssetDepot and immutable authoring generations.
own::shared_owner<const ModelMeshDescriptor> LoadModelStreamDescriptor(
    experiment::cooked::ResolvedAssetEntry resolved,
    own::shared_owner<const ModelSkeletonPayload> skeleton, std::string& error);
own::shared_owner<const ModelSkeletonPayload> LoadModelStreamSkeleton(
    experiment::cooked::ResolvedAssetEntry resolved, std::string& error);
}
