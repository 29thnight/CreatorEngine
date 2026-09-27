#include "LXNodeDefinition.h"

#include <algorithm>
#include <utility>

namespace LX
{
namespace
{
bool SamePinSchema(const Pin& first, const Pin& second)
{
    return first.Identifier() == second.Identifier() && first.name == second.name &&
           first.direction == second.direction && first.type == second.type && first.multiple == second.multiple &&
           !first.dynamic && !second.dynamic;
}
} // namespace

LXNodeDefinitionRegistry::LXNodeDefinitionRegistry()
{
    RegisterDomain("material", {PinType::Bool, PinType::Int, PinType::Float, PinType::Vector, PinType::Color,
                                PinType::Normal, PinType::Texture, PinType::Surface});
    RegisterDomain("behavior", {PinType::Flow, PinType::Bool, PinType::Int, PinType::Float, PinType::Vector,
                                PinType::Color, PinType::Normal, PinType::Texture});
    RegisterDomain("animation", {PinType::Flow, PinType::Bool, PinType::Int, PinType::Float, PinType::Vector,
                                 PinType::Color, PinType::Normal, PinType::Texture});
}

bool LXNodeDefinitionRegistry::RegisterDomain(const std::string& name, std::set<PinType> allowedPins)
{
    if (name.empty() || allowedPins.empty())
    {
        return false;
    }
    return domains_.emplace(name, std::move(allowedPins)).second;
}

bool LXNodeDefinitionRegistry::Register(LXNodeDefinition definition, std::string* error)
{
    const NodeSpec& spec = definition.defaults;
    if (definition.domain.empty() || !HasDomain(definition.domain) || spec.type.empty() || spec.title.empty() ||
        definitions_.contains(spec.type))
    {
        if (error)
        {
            *error = "Missing domain/type/title or duplicate node definition";
        }
        return false;
    }
    for (std::size_t index = 0; index < spec.pins.size(); ++index)
    {
        const Pin& pin = spec.pins[index];
        if (pin.name.empty() || pin.dynamic || !AllowsPin(definition.domain, pin.type) ||
            !IsSocketValueValid(pin.type, pin.value) ||
            std::any_of(spec.pins.begin(), spec.pins.begin() + index, [&](const Pin& previous) {
                return previous.direction == pin.direction && previous.Identifier() == pin.Identifier();
            }))
        {
            if (error)
            {
                *error = "Invalid fixed pin schema";
            }
            return false;
        }
    }
    if (spec.dynamicPins && (!spec.dynamicPins->maxCount || !AllowsPin(definition.domain, spec.dynamicPins->type)))
    {
        if (error)
        {
            *error = "Invalid dynamic pin rule";
        }
        return false;
    }
    if (std::any_of(spec.properties.begin(), spec.properties.end(),
                    [](const auto& property) { return property.first.empty(); }))
    {
        if (error)
        {
            *error = "Invalid default property name";
        }
        return false;
    }
    const std::string type = spec.type;
    definitions_.emplace(type, std::move(definition));
    return true;
}

const LXNodeDefinition* LXNodeDefinitionRegistry::Find(const std::string& type) const
{
    const auto found = definitions_.find(type);
    return found == definitions_.end() ? nullptr : &found->second;
}

bool LXNodeDefinitionRegistry::HasDomain(const std::string& domain) const
{
    return domains_.contains(domain);
}

bool LXNodeDefinitionRegistry::AllowsPin(const std::string& domain, PinType type) const
{
    const auto found = domains_.find(domain);
    return found != domains_.end() && found->second.contains(type);
}

bool LXNodeDefinitionRegistry::MatchesSpec(const std::string& domain, const NodeSpec& spec) const
{
    const LXNodeDefinition* definition = Find(spec.type);
    if (!definition || !definition->creatable || definition->domain != domain ||
        definition->defaults.pins.size() != spec.pins.size() || definition->defaults.dynamicPins != spec.dynamicPins)
    {
        return false;
    }
    for (std::size_t index = 0; index < spec.pins.size(); ++index)
    {
        if (!SamePinSchema(definition->defaults.pins[index], spec.pins[index]))
        {
            return false;
        }
        if (definition->defaults.pins[index].value != spec.pins[index].value)
        {
            return false;
        }
    }
    return true;
}

bool LXNodeDefinitionRegistry::MatchesNode(const std::string& domain, const Node& node) const
{
    const LXNodeDefinition* definition = Find(node.type);
    if (!definition || definition->domain != domain || node.dynamicPins != definition->defaults.dynamicPins ||
        node.pins.size() < definition->defaults.pins.size())
    {
        return false;
    }
    for (std::size_t index = 0; index < node.pins.size(); ++index)
    {
        const Pin& pin = node.pins[index];
        if (index < definition->defaults.pins.size())
        {
            if (!SamePinSchema(definition->defaults.pins[index], pin))
            {
                return false;
            }
        }
        else if (!pin.dynamic || !node.dynamicPins || pin.direction != node.dynamicPins->direction ||
                 pin.type != node.dynamicPins->type || pin.multiple != node.dynamicPins->multiple)
        {
            return false;
        }
    }
    for (const auto& [key, value] : definition->defaults.properties)
    {
        if (!node.properties.contains(key))
        {
            return false;
        }
    }
    return true;
}

} // namespace LX
