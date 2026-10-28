#include "../../Containers/ContainerAPI.h"
#pragma once
#if defined(CE_CONTRACT_STANDALONE)
#include "../../../Tools/build/ModuleAdapters/Creator.Core.Ownership.h"
#else
import Creator.Core.Ownership;
#endif

#include "ModelAnimationDescriptor.h"
#include "ModelMeshDescriptor.h"
#include "ModelSkeletonPayload.h"

#include "../../Utility_Framework/Foundation/StandardLibrary.h"

class Material;

namespace assets
{
    struct ModelGeometryPayload;
    struct ModelCpuGeometryPayload;

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
        ce::dynamic_array<own::shared_owner<const ModelMeshDescriptor>> meshes{};
        ce::dynamic_array<own::shared_owner<const Material>> materials{};
        own::shared_owner<const ModelSkeletonPayload> skeleton{};
        // The effective choice is fixed during preparation, before geometry
        // requests are admitted. Scene construction cannot add hidden I/O.
        ModelColliderPreparationPolicy colliderPolicy{ ModelColliderPreparationPolicy::CookedDefault };
        bool createMeshCollider{};
        // Empty when the effective collider policy is disabled. Otherwise follows
        // summary.meshes with null entries for uninstantiated meshes. These are
        // transient collision inputs; components retain only mesh descriptions.
        ce::dynamic_array<own::shared_owner<const ModelCpuGeometryPayload>> geometry{};
    };
}
