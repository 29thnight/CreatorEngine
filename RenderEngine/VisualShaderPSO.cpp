#include "VisualShaderPSO.h"

#include <utility>

namespace
{
std::unordered_map<std::string, std::size_t> BuildNodeIndex(const VisualShaderGraphDesc& graph)
{
    std::unordered_map<std::string, std::size_t> result;
    result.reserve(graph.nodes.size());
    for (std::size_t i = 0; i < graph.nodes.size(); ++i)
    {
        result.emplace(graph.nodes[i].id, i);
    }
    return result;
}
}

VisualShaderPSO::VisualShaderPSO(std::shared_ptr<ShaderPSO> basePso,
                                 VisualShaderGraphDesc graphDesc,
                                 ShaderAssetDesc baseDesc)
    : m_basePSO(std::move(basePso))
    , m_graphDesc(std::move(graphDesc))
    , m_baseShaderDesc(std::move(baseDesc))
    , m_nodeIndexById(BuildNodeIndex(m_graphDesc))
{
}

const VisualShaderNodeDesc* VisualShaderPSO::FindNode(std::string_view nodeId) const
{
    const auto it = m_nodeIndexById.find(std::string(nodeId));
    if (it == m_nodeIndexById.end())
    {
        return nullptr;
    }
    return &m_graphDesc.nodes[it->second];
}
