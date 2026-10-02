#pragma once
#include "../Utility_Framework/AuthoringReadNode.h"
#include "../Utility_Framework/LayerCatalog.h"

namespace ce::layers
{
inline result<layer_id> ReadEntityLayer(const Authoring::ReadNode& node, const catalog_snapshot& catalog)
{
    if (!node.IsMap() || node["m_layer"] || node["m_collisionType"] || !node["m_layerId"].IsScalar())
        return std::unexpected(error::invalid_definition);

    std::uint64_t id = 0;
    if (!Authoring::Scalar::TryParseUInt64(node["m_layerId"].AsString(), id) || !catalog.Find(layer_id{id}))
        return std::unexpected(error::unknown_layer);

    return layer_id{id};
}
} // namespace ce::layers
