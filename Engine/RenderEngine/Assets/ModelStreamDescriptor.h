#pragma once
#if defined(CE_CONTRACT_STANDALONE)
#include "../../../Tools/build/ModuleAdapters/Creator.Core.Ownership.h"
#else
import Creator.Core.Ownership;
#endif
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
