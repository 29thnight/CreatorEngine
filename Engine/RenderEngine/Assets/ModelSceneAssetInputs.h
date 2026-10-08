#pragma once

#include "ModelAnimationDescriptor.h"
#include "ModelMeshDescriptor.h"
#include "ModelSkeletonPayload.h"

class Material;

namespace assets
{
    // Preparation result, not another cached asset or model bulk aggregate.
    // Ordered values follow descriptor.summary; each child keeps its own exact
    // generation and source. Geometry/images/clips remain separately resident.
    struct ModelSceneAssetInputs final
    {
        own::shared_owner<const ModelAnimationDescriptor> descriptor{};
        std::vector<own::shared_owner<const ModelMeshDescriptor>> meshes{};
        std::vector<own::shared_owner<const Material>> materials{};
        own::shared_owner<const ModelSkeletonPayload> skeleton{};
    };
}
