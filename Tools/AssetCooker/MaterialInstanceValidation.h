#pragma once

#include "MaterialGraphRuntime.h"
#include "AuthoringReadNode.h"

#include <algorithm>
#include <map>

namespace material_cook
{
using Programs = std::map<experiment::AssetId, material_graph::VerifiedProduct>;

inline bool ValidateInstances(const Authoring::ReadNode& node, const Programs& programs,
                              const experiment::cooked::CookedAssetManifest& manifest, std::string& error)
{
    if (node.IsMap() && node["lattice_material"])
    {
        material_graph::InstanceDocument document;
        if (!material_graph::ReadInstanceDocument(node, document, error))
        {
            return false;
        }
        const auto graph = programs.find(document.description.graphId);
        if (graph == programs.end())
        {
            error = "Lattice material references a graph outside the cooked MaterialProgram closure.";
            return false;
        }
        std::vector<std::uint8_t> uniforms;
        std::vector<LX::LXMaterialDiagnostic> diagnostics;
        if (!material_graph::PrepareUniforms(graph->second.layout, document.description.parameters, uniforms,
                                             diagnostics))
        {
            error = diagnostics.empty() ? "Invalid cooked material numeric override." : diagnostics.front().message;
            return false;
        }
        for (const auto& texture : document.description.textures)
        {
            const auto artifact = std::ranges::find(manifest.entries, texture.assetId,
                                                    &experiment::cooked::CookedAssetManifestEntry::assetId);
            const auto& program = graph->second.program;
            const auto parameter =
                std::ranges::find(program.parameters, texture.parameter, &LX::LXMaterialParameter::id);
            if (artifact == manifest.entries.end() || artifact->kind != experiment::cooked::CookedAssetKind::Texture ||
                parameter == program.parameters.end() || !parameter->exposed ||
                parameter->type != LX::PinType::Texture ||
                std::ranges::none_of(graph->second.layout.textures,
                                     [&](const auto& resource) { return resource.parameter == texture.parameter; }))
            {
                error = "Cooked texture override requires an exposed, active Texture parameter.";
                return false;
            }
        }
        return true;
    }
    if (node.IsMap())
    {
        for (const auto entry : node.Map())
        {
            if (!ValidateInstances(entry.value, programs, manifest, error))
            {
                return false;
            }
        }
    }
    else if (node.IsSequence())
    {
        for (const auto entry : node)
        {
            if (!ValidateInstances(entry, programs, manifest, error))
            {
                return false;
            }
        }
    }
    return true;
}
} // namespace material_cook
