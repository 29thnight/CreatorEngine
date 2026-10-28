#pragma once
#include "../../Engine/Utility_Framework/Foundation/EngineOrderedMap.h"
#include "../../Engine/Utility_Framework/Foundation/EngineOrderedSet.h"

#include "LXGraph.h"

#include "../../Engine/Utility_Framework/Foundation/StandardLibrary.h"

namespace LX
{

struct LXNodeDefinition
{
    std::string domain;
    NodeSpec defaults;
    bool creatable = true;
};

class LXNodeDefinitionRegistry
{
  public:
    LXNodeDefinitionRegistry();

    bool RegisterDomain(const std::string& name, ce::engine_ordered_set<PinType> allowedPins);
    bool Register(LXNodeDefinition definition, std::string* error = nullptr);
    const LXNodeDefinition* Find(const std::string& type) const;
    bool HasDomain(const std::string& domain) const;
    bool AllowsPin(const std::string& domain, PinType type) const;
    bool MatchesSpec(const std::string& domain, const NodeSpec& spec) const;
    bool MatchesNode(const std::string& domain, const Node& node) const;

  private:
    ce::engine_ordered_map<std::string, ce::engine_ordered_set<PinType>> domains_;
    ce::engine_ordered_map<std::string, LXNodeDefinition> definitions_;
};

} // namespace LX
