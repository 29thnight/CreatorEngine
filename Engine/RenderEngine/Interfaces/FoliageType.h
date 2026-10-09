#pragma once
#include "Ownership.h"
#include "Reflection.hpp"
#include "TypeTrait.h"
#include "../AssetDepot/AssetLink.h"
#include <cstdint>
#include <memory>
#include <string>

class Material;
namespace material_graph
{
    struct SceneMaterialSource;
}
namespace experiment
{
    struct Material;
}
namespace assets
{
    class ModelAssetGeneration;
    struct ModelAnimationDescriptor;
    struct ModelMeshDescriptor;
}

struct [[reflgen::reflect]] FoliageType
{
public:
    // Serialized values, interpreted only through their expected typed links.
    // Names are labels / old authoring migration input, never mounted identity.
    FileGuid m_modelGuid{};
    FileGuid m_meshAssetId{};
    FileGuid m_materialAssetId{};
    bool m_allowLegacySource{ false };
    bool m_castShadow{ true };
    bool m_isShadowRecive{ true };
    std::string m_modelName{};

    [[nodiscard]] AssetDepot::AssetLink<assets::ModelAnimationDescriptor> ModelLink() const
    {
        return { { experiment::AssetId{ m_modelGuid.m_guid }, {} } };
    }
    [[nodiscard]] AssetDepot::AssetLink<assets::ModelMeshDescriptor> MeshLink() const
    {
        return { { experiment::AssetId{ m_meshAssetId.m_guid }, {} } };
    }
    [[nodiscard]] AssetDepot::AssetLink<Material> MaterialLink() const
    {
        return { { experiment::AssetId{ m_materialAssetId.m_guid }, {} } };
    }

    // Immutable published owners only. Requests belong to the component and
    // cannot be copied into render proxies or cancel another subscriber there.
    [[reflgen::ignore]]
    own::shared_owner<const assets::ModelAnimationDescriptor> m_modelDescriptor{};
    [[reflgen::ignore]]
    own::shared_owner<const assets::ModelMeshDescriptor> m_meshDescriptor{};
    [[reflgen::ignore]]
    own::shared_owner<const Material> m_material{};
    [[reflgen::ignore]]
    own::shared_owner<const material_graph::SceneMaterialSource> m_graphMaterialSource{};
    [[reflgen::ignore]]
    own::shared_owner<const experiment::Material> m_authoredMaterial{};

    // Explicit, unmounted authoring fallback only. Mounted bindings never own
    // whole-model geometry, images or unrelated clips through this field.
    [[reflgen::ignore]]
    own::shared_owner<const assets::ModelAssetGeneration> m_modelGeneration{};
    [[reflgen::ignore]]
    std::uint32_t m_modelMeshIndex{};

    FoliageType() = default;
    explicit FoliageType(const std::string& modelName, bool castShadow = true)
        : m_allowLegacySource(true), m_castShadow(castShadow), m_modelName(modelName)
    {
    }
    bool operator==(const FoliageType& other) const
    {
        return m_modelGuid == other.m_modelGuid && m_meshAssetId == other.m_meshAssetId
            && m_materialAssetId == other.m_materialAssetId
            && m_allowLegacySource == other.m_allowLegacySource
            && m_modelName == other.m_modelName && m_castShadow == other.m_castShadow
            && m_isShadowRecive == other.m_isShadowRecive;
    }
};
