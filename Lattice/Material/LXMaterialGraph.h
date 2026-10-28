#include "../../Engine/Containers/ContainerAPI.h"
#pragma once
#include "../../Engine/Utility_Framework/Foundation/EngineOrderedMap.h"
#include "../../Engine/Utility_Framework/Foundation/EngineOrderedSet.h"

#include "../Core/LXNodeDefinition.h"

#include "../../Engine/Utility_Framework/Foundation/StandardLibrary.h"

namespace LX
{

enum class LXColorSpace : std::uint8_t
{
    Data,
    Linear,
    SRGB
};

struct LXMaterialSocketState
{
    bool enabled = true;
    bool hidden = false;
    bool hideValue = false;
    LXColorSpace colorSpace = LXColorSpace::Data;

    bool operator==(const LXMaterialSocketState&) const = default;
};

struct LXMaterialNodeState
{
    std::uint32_t schemaRevision = 1;
    // Unknown definitions stay read-only in LXGraph. Keep their entire archive record, including extension fields.
    std::string unknownPayload;

    bool operator==(const LXMaterialNodeState&) const = default;
};

struct LXMaterialScopeState
{
    ce::engine_ordered_map<Id, LXMaterialNodeState> nodes;
    ce::engine_ordered_map<Id, LXMaterialSocketState> sockets;

    bool operator==(const LXMaterialScopeState&) const = default;
};

struct LXMaterialNodeSchema
{
    std::uint32_t revision = 1;
    ce::dynamic_array<LXMaterialSocketState> sockets;
    ce::engine_ordered_map<std::string, ce::engine_ordered_set<std::string>> enumProperties;
};

struct LXMaterialDefinitions
{
    std::shared_ptr<const LXNodeDefinitionRegistry> nodes;
    ce::engine_ordered_map<std::string, LXMaterialNodeSchema> schemas;
};

LXMaterialDefinitions CreateMaterialDefinitions();

struct LXMaterialParameter
{
    Id id = 0;
    std::string identifier;
    std::string name;
    PinType type = PinType::Float;
    LXSocketValue value;
    LXColorSpace colorSpace = LXColorSpace::Data;
    bool exposed = true;

    bool operator==(const LXMaterialParameter&) const = default;
};

class LXMaterialAsset
{
  public:
    explicit LXMaterialAsset(LXMaterialDefinitions definitions = CreateMaterialDefinitions());

    Id graphId = 1;
    Id activeOutput = 0;
    LXGraph graph;
    ce::dynamic_array<LXMaterialParameter> blackboard;
    // Scope 0 is the root; other scope IDs are stable shared group-definition IDs.
    ce::engine_ordered_map<Id, LXMaterialScopeState> scopes;

    const LXMaterialDefinitions& Definitions() const { return definitions_; }
    Id CreateNode(const std::string& type, float x, float y, std::string title = {});
    Id CollapseToGroup(const ce::dynamic_array<Id>& nodes, std::string name);
    LXMaterialSocketState SocketState(Id scope, const Pin& pin) const;
    LXMaterialNodeState NodeState(Id scope, const Node& node) const;
    ce::dynamic_array<Issue> Validate() const;
    bool Equals(const LXMaterialAsset& other) const;
    bool Save(const std::filesystem::path& path, std::string* error = nullptr) const;
    static std::optional<LXMaterialAsset> Load(const std::filesystem::path& path,
                                               const LXMaterialDefinitions& definitions, std::string* error = nullptr);

  private:
    LXMaterialDefinitions definitions_;
};

// Structured authoring archive; deliberately separate from the independent example's LXG format.
class LXMaterialArchive
{
  public:
    static constexpr std::uint32_t SchemaVersion = 1;
    static std::string Write(const LXMaterialAsset& asset);
    // 실행 입력의 정규 표현이다. 배치·화면 상태와 ID 할당기는 포함하지 않는다.
    static std::string WriteSource(const LXMaterialAsset& asset);
    static std::optional<LXMaterialAsset> Read(const std::string& text, const LXMaterialDefinitions& definitions,
                                               std::string* error = nullptr);

  private:
    friend class LXMaterialAsset;
    static std::string WriteDocument(const LXMaterialAsset& asset, bool includeLayout);
    struct Reader;
};

} // namespace LX
