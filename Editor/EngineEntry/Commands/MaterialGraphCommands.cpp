#include "CommandRegistrar.h"
#include "CommandSupport.h"
#include "ConsoleCommandSystem.h"
#include "EditorObjectOperations.h"
#include "ReflectionUndo.h"
#include "SceneManager.h"
#include "Scene.h"
#include "MeshRenderer.h"
#include "Material.h"
#include "DataSystem.h"
#include "MaterialGraphWindow.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <sstream>

namespace ConsoleCmd
{
namespace
{
using namespace CommandCore;

CommandData DescribeGraph(const MeshRenderer& renderer)
{
    auto data = CommandData::Object();
    const auto instance = renderer.m_Material ? renderer.m_Material->GetMaterialGraphInstance() : nullptr;
    data.Set("enabled", CommandData::Bool(!!instance));
    if (!instance)
    {
        return data;
    }
    data.Set("graph", CommandData::String(FileGuid(instance->description.graphId.value).ToString()));
    data.Set("generation", CommandData::Int(instance->generation->generation));
    data.Set("features", CommandData::Int(instance->generation->cooked.product.program.features));
    data.Set("renderableMesh", CommandData::Bool(renderer.HasRenderableMesh()));
    auto uniforms = CommandData::Array();
    for (auto byte : instance->uniforms)
    {
        uniforms.Append(CommandData::Int(byte));
    }
    data.Set("uniformBytes", std::move(uniforms));
    auto parameters = CommandData::Array();
    for (const auto& parameter : instance->description.parameters)
    {
        auto entry = CommandData::Object();
        entry.Set("id", CommandData::Int(parameter.id));
        std::visit(
            [&](const auto& value) {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, std::int64_t>)
                {
                    entry.Set("value", CommandData::Int(value));
                }
                else if constexpr (std::is_same_v<T, double>)
                {
                    entry.Set("value", CommandData::Double(value));
                }
                else if constexpr (std::is_same_v<T, bool>)
                {
                    entry.Set("value", CommandData::Bool(value));
                }
                else if constexpr (requires {
                                       value.begin();
                                       value.end();
                                   } && !std::is_same_v<T, std::string>)
                {
                    auto components = CommandData::Array();
                    for (double component : value)
                    {
                        components.Append(CommandData::Double(component));
                    }
                    entry.Set("value", std::move(components));
                }
            },
            parameter.value);
        parameters.Append(std::move(entry));
    }
    data.Set("parameters", std::move(parameters));
    auto textures = CommandData::Array();
    for (const auto& texture : instance->textures)
    {
        auto entry = CommandData::Object();
        entry.Set("slot", CommandData::Int(texture.slot));
        entry.Set("asset", CommandData::String(FileGuid(texture.assetId.value).ToString()));
        textures.Append(std::move(entry));
    }
    data.Set("textures", std::move(textures));
    return data;
}

CommandResult MaterialGraph(const ConsoleCommandContext& context)
{
    const auto& parts = context.parts;
    if (parts.size() < 2)
    {
        return InvalidArguments("material.graph <object> [bind <graph-guid>|set <parameter-id> <value...>|reload]");
    }
    EntityHandle target;
    auto resolved = EditorObjectOperations::ResolveTarget(parts[1], target);
    if (!resolved.IsSuccess())
    {
        return resolved;
    }
    auto* scene = SceneManagers->GetActiveScene();
    auto* entity = scene ? scene->Resolve(target) : nullptr;
    auto* renderer = entity ? entity->GetComponent<MeshRenderer>() : nullptr;
    if (!renderer || !renderer->m_Material)
    {
        return PreconditionFailed("material.not_found", "Target has no MeshRenderer material");
    }
    if (parts.size() == 2)
    {
        return Ok({}, DescribeGraph(*renderer));
    }
    if (EditorObjectOperations::IsEditLocked(entity, true))
    {
        return PreconditionFailed("object.locked", "Unlock the entity hierarchy before editing");
    }
    if (renderer->m_materialBaseGuid != FileGuid{})
    {
        return PreconditionFailed("material.inline_required", "Graph editing requires an inline material instance");
    }
    const auto before = renderer->m_Material;
    auto candidate = std::make_shared<Material>(*before);
    std::string error;
    if (parts[2] == "bind" && parts.size() == 4)
    {
        material_graph::InstanceDescription description;
        if (!Uuid::TryParse(parts[3], description.graphId.value))
        {
            return InvalidArguments("Graph GUID is invalid");
        }
        if (!DataSystems->ConfigureMaterialGraph(*candidate, description, error))
        {
            return Fail("material.graph.rejected", error);
        }
    }
    else if (parts[2] == "reload" && parts.size() == 3 && candidate->HasMaterialGraph())
    {
        const auto description = candidate->GetMaterialGraphInstance()->description;
        if (!DataSystems->ConfigureMaterialGraph(*candidate, description, error, true))
        {
            return Fail("material.graph.rejected", error);
        }
    }
    else if (parts[2] == "set" && parts.size() >= 5 && parts.size() <= 8 && candidate->HasMaterialGraph())
    {
        LX::Id id{};
        if (!ParseNumber(parts[3], id) || id == 0)
        {
            return InvalidArguments("Parameter ID is invalid");
        }
        LX::LXSocketValue value;
        const auto& parameters = candidate->GetMaterialGraphInstance()->generation->cooked.product.program.parameters;
        const auto parameter =
            std::find_if(parameters.begin(), parameters.end(), [id](const auto& entry) { return entry.id == id; });
        if (parameter == parameters.end())
        {
            return Fail("material.graph.rejected", "Parameter ID is not declared by this graph");
        }
        std::array<double, 4> numbers{};
        const auto count = parts.size() - 4;
        if (count == 2)
        {
            return InvalidArguments("Vector parameters require three components; Color requires four");
        }
        if (count == 1 && parameter->type == LX::PinType::Int)
        {
            std::int64_t integer{};
            if (!ParseNumber(parts[4], integer))
            {
                return InvalidArguments("Integer parameter requires an exact signed integer");
            }
            value = integer;
        }
        else if (count == 1 && (parts[4] == "true" || parts[4] == "false"))
        {
            value = parts[4] == "true";
        }
        else
        {
            for (std::size_t index = 0; index < count; ++index)
            {
                if (!ParseNumber(parts[4 + index], numbers[index]) || !std::isfinite(numbers[index]))
                {
                    return InvalidArguments("Parameter components must be finite numbers");
                }
            }
            if (count == 1)
            {
                value = numbers[0];
            }
            if (count == 3)
            {
                value = std::array<double, 3>{numbers[0], numbers[1], numbers[2]};
            }
            if (count == 4)
            {
                value = numbers;
            }
        }
        if (!candidate->TrySetMaterialGraphParameter(id, std::move(value), error))
        {
            return Fail("material.graph.rejected", error);
        }
    }
    else
    {
        return InvalidArguments("material.graph <object> [bind <graph-guid>|set <parameter-id> <value...>|reload]");
    }
    const auto apply = [target](const std::shared_ptr<Material>& material) {
        auto* active = SceneManagers->GetActiveScene();
        auto* object = active ? active->Resolve(target) : nullptr;
        if (auto* component = object ? object->GetComponent<MeshRenderer>() : nullptr)
        {
            component->SetMaterial(material);
        }
    };
    Meta::MakeCustomChangeCommand([apply, before] { apply(before); }, [apply, candidate] { apply(candidate); });
    return Ok("Graph material instance updated", DescribeGraph(*renderer));
}
} // namespace

void RegisterMaterialGraphCommands(Registrar& registrar)
{
    registrar.Result({"material.graph"}, &MaterialGraph);
    registrar.Result({"material.editor"}, [](const ConsoleCommandContext& context) {
        return editor::material_editing::Command(context.parts);
    });
}
} // namespace ConsoleCmd
