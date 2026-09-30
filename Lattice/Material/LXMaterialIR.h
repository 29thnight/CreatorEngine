#pragma once

#include "LXMaterialGraph.h"

#include <functional>

namespace LX
{

struct LXMaterialIRSocket
{
    Id id = 0;
    Id interfaceId = 0;
    std::string identifier;
    Direction direction = Direction::Input;
    PinType type = PinType::Float;
    bool multiple = false;
    LXSocketValue value;
    LXMaterialSocketState state;

    bool operator==(const LXMaterialIRSocket&) const = default;
};

struct LXMaterialIRNode
{
    Id id = 0;
    Id groupId = 0;
    std::string definition;
    std::uint32_t schemaRevision = 1;
    std::vector<LXMaterialIRSocket> sockets;
    std::map<std::string, std::string> properties;

    bool operator==(const LXMaterialIRNode&) const = default;
};

struct LXMaterialIRLink
{
    Id id = 0;
    Id output = 0;
    Id input = 0;
    PinType type = PinType::Float;

    bool operator==(const LXMaterialIRLink&) const = default;
};

struct LXMaterialIRScope
{
    Id groupId = 0;
    std::vector<LXGroupSocket> groupInterface;
    // Dependency order, with stable node ID as the tie breaker. Socket order is definition order.
    std::vector<LXMaterialIRNode> nodes;
    std::vector<LXMaterialIRLink> links;

    bool operator==(const LXMaterialIRScope&) const = default;
};

struct LXMaterialIR
{
    Id graphId = 0;
    Id activeOutput = 0;
    std::vector<LXMaterialParameter> blackboard;
    std::vector<LXMaterialIRScope> scopes;

    bool operator==(const LXMaterialIR&) const = default;
};

std::optional<LXMaterialIR> BuildMaterialIR(const LXMaterialAsset& asset, std::vector<Issue>* issues = nullptr);

class LXMaterialDocument
{
  public:
    explicit LXMaterialDocument(LXMaterialAsset asset);

    LXMaterialAsset& Asset() { return asset_; }
    const LXMaterialAsset& Asset() const { return asset_; }
    const std::optional<LXMaterialIR>& LastValidIR() const { return lastValidIR_; }
    std::uint64_t Generation() const { return generation_; }
    bool PublishIR(std::vector<Issue>* issues = nullptr);
    bool Reload(const std::filesystem::path& path, std::string* error = nullptr);
    // The Material host must transact the entire asset, including socket metadata and Blackboard.
    bool Edit(const std::function<bool(LXMaterialAsset&)>& operation);
    bool Undo();
    bool Redo();

  private:
    LXMaterialAsset asset_;
    std::optional<LXMaterialIR> lastValidIR_;
    std::uint64_t generation_ = 0;
    std::vector<LXMaterialAsset> undo_;
    std::vector<LXMaterialAsset> redo_;
};

} // namespace LX
