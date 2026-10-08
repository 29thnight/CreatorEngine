#pragma once

#include "ModelAnimationDescriptor.h"
#include "ModelMeshDescriptor.h"
#include "ModelSkeletonPayload.h"

#include <cstdint>

class Material;

namespace assets
{
    struct ModelGeometryPayload;

    enum class ModelColliderPreparationPolicy : std::uint8_t
    {
        CookedDefault,
        Enabled,
        Disabled,
    };

    [[nodiscard]] constexpr bool ShouldPrepareModelCollider(
        ModelColliderPreparationPolicy policy, bool cookedDefault) noexcept
    {
        return policy == ModelColliderPreparationPolicy::Enabled ||
            (policy == ModelColliderPreparationPolicy::CookedDefault && cookedDefault);
    }

    // Preparation result, not another cached asset or model bulk aggregate.
    // Ordered values follow descriptor.summary; each child keeps its own exact
    // generation and source. Geometry/images/clips remain separately resident.
    struct ModelSceneAssetInputs final
    {
        own::shared_owner<const ModelAnimationDescriptor> descriptor{};
        std::vector<own::shared_owner<const ModelMeshDescriptor>> meshes{};
        std::vector<own::shared_owner<const Material>> materials{};
        own::shared_owner<const ModelSkeletonPayload> skeleton{};
        // The effective choice is fixed during preparation, before geometry
        // requests are admitted. Scene construction cannot add hidden I/O.
        ModelColliderPreparationPolicy colliderPolicy{ ModelColliderPreparationPolicy::CookedDefault };
        bool createMeshCollider{};
        // Empty when the effective collider policy is disabled. Otherwise follows
        // summary.meshes with null entries for uninstantiated meshes. These are
        // transient collision inputs; components retain only mesh descriptions.
        std::vector<own::shared_owner<const ModelGeometryPayload>> geometry{};
    };
}
