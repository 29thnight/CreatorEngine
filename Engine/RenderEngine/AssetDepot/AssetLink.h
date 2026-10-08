#pragma once

#include "../Experiment/Cooked/CookedAssetManifest.h"
#include "../Assets/AssetIdentityProfile.h"

#include <type_traits>

class Texture;
class Material;
struct ShaderMeta;
namespace assets
{
    class ModelAssetGeneration;
    struct ModelAnimationDescriptor;
    struct ModelSkeletonPayload;
    struct ModelAnimationPayload;
    struct ModelMeshDescriptor;
    struct ModelGeometryPayload;
}
namespace experiment
{
    struct Material;
}

namespace AssetDepot
{
    // Register an engine type explicitly. Type identity is stable manifest data;
    // neither RTTI names nor native pointer values enter serialized references.
    template<class T>
    struct AssetTypeTraits;

    template<>
    struct AssetTypeTraits<Texture>
    {
        static constexpr auto kKind = experiment::cooked::CookedAssetKind::Texture;
    };

    template<>
    struct AssetTypeTraits<Material>
    {
        static constexpr auto kKind = experiment::cooked::CookedAssetKind::Material;
    };

    template<>
    struct AssetTypeTraits<experiment::Material>
    {
        static constexpr auto kKind = experiment::cooked::CookedAssetKind::Material;
    };

    template<>
    struct AssetTypeTraits<ShaderMeta>
    {
        static constexpr auto kKind = experiment::cooked::CookedAssetKind::ShaderMeta;
    };

    template<>
    struct AssetTypeTraits<assets::ModelAssetGeneration>
    {
        static constexpr auto kKind = experiment::cooked::CookedAssetKind::Model;
    };

    template<>
    struct AssetTypeTraits<assets::ModelAnimationDescriptor>
    {
        static constexpr auto kKind = experiment::cooked::CookedAssetKind::Model;
    };

    template<>
    struct AssetTypeTraits<assets::ModelSkeletonPayload>
    {
        static constexpr auto kKind = experiment::cooked::CookedAssetKind::Skeleton;
    };

    template<>
    struct AssetTypeTraits<assets::ModelAnimationPayload>
    {
        static constexpr auto kKind = experiment::cooked::CookedAssetKind::AnimationClip;
    };

    template<>
    struct AssetTypeTraits<assets::ModelMeshDescriptor>
    {
        static constexpr auto kKind = experiment::cooked::CookedAssetKind::Mesh;
    };

    // Internal exact-payload dispatch only. Current links acquire descriptors.
    template<>
    struct AssetTypeTraits<assets::ModelGeometryPayload>
    {
        static constexpr auto kKind = experiment::cooked::CookedAssetKind::Mesh;
    };

    // Persist only the asset/subasset IDs plus the expected kind at the wire
    // boundary. A link is not a resident pin, weak owner or load request.
    template<class T>
    struct AssetLink final
    {
        experiment::cooked::AssetIdentity identity{};
        static constexpr auto kKind = AssetTypeTraits<std::remove_cv_t<T>>::kKind;

        [[nodiscard]] bool IsValid() const noexcept
        {
            const auto valid = [](const experiment::AssetId& id)
            {
                return experiment::IsAssetIdV4(id) || assets::IsUuidV8(id.value);
            };
            return valid(identity.assetId)
                && (!identity.subassetId.IsValid() || valid(identity.subassetId));
        }

        [[nodiscard]] experiment::cooked::TypedAssetReference ToReference() const noexcept
        {
            return { identity, kKind };
        }

        // Failed typed decode preserves the caller's previous value.
        [[nodiscard]] static bool FromReference(
            const experiment::cooked::TypedAssetReference& reference, AssetLink& out) noexcept
        {
            AssetLink candidate{ reference.key };
            if (reference.kind != kKind || !candidate.IsValid())
            {
                return false;
            }
            out = candidate;
            return true;
        }

        friend auto operator<=>(const AssetLink&, const AssetLink&) noexcept = default;
    };
}
