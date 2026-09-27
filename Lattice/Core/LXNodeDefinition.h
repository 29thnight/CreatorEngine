#pragma once

#include "LXGraph.h"

#include <map>
#include <set>
#include <string>

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

    bool RegisterDomain(const std::string& name, std::set<PinType> allowedPins);
    bool Register(LXNodeDefinition definition, std::string* error = nullptr);
    const LXNodeDefinition* Find(const std::string& type) const;
    bool HasDomain(const std::string& domain) const;
    bool AllowsPin(const std::string& domain, PinType type) const;
    bool MatchesSpec(const std::string& domain, const NodeSpec& spec) const;
    bool MatchesNode(const std::string& domain, const Node& node) const;

  private:
    std::map<std::string, std::set<PinType>> domains_;
    std::map<std::string, LXNodeDefinition> definitions_;
};

} // namespace LX
