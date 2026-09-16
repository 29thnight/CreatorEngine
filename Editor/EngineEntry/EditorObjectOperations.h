#pragma once
#include "CommandCore/CommandResult.h"
#include "EntityHandle.h"
#include "AuthoringWriteNode.h"
#include <memory>
#include <string>
#include <vector>
#include <mathematics/vector3.hpp>
#include <mathematics/quaternion.hpp>
class Scene;
class Entity;
class Component;
class Prefab;
class Material;
enum class MaterialRenderingMode;
enum class ValueType : std::uint16_t;
class Animator;
enum class GameObjectType;

namespace EditorObjectOperations
{
    // Runtime identity: @scene:index:generation. Names are lookup input, not identity.
    std::string ObjectId(EntityHandle target);
    CommandCore::CommandResult ResolveTarget(const std::string& nameOrId, EntityHandle& target);
    CommandCore::CommandResult Describe(EntityHandle target);
    CommandCore::CommandResult Properties(EntityHandle target, const std::string& component);
    CommandCore::CommandResult Rename(EntityHandle target, const std::string& name);
    CommandCore::CommandResult SetIcon(EntityHandle target, const std::string& preset);
    CommandCore::CommandResult SetEditLocked(EntityHandle target, bool locked);
    // Ancestor locks protect descendants; subtree checks also protect locked
    // children from indirect edits such as moving/deleting their parent.
    bool IsEditLocked(const Entity* target, bool includeDescendants = false);
    CommandCore::CommandResult Create(Scene* scene, const std::string& name, GameObjectType type, uint32_t parent = 0);
    CommandCore::CommandResult Delete(EntityHandle target);
    CommandCore::CommandResult Duplicate(EntityHandle target, const std::string& name = {});
    CommandCore::CommandResult Parent(EntityHandle target, EntityHandle parent);
    CommandCore::CommandResult Transform(EntityHandle target, math::vector3 position, math::quaternion rotation, math::vector3 scale);
    // A GUI drag keeps handles and owned values, never component pointers. One
    // commit groups all affected objects into a single Undo entry.
    struct PropertyEdit
    {
        EntityHandle target;
        std::string component;
        std::vector<std::string> fields;
        Authoring::WriteDocument before;
    };
    PropertyEdit CapturePropertyEdit(Component& component, std::vector<std::string> fields);
    bool CommitPropertyEdits(std::vector<PropertyEdit> edits);
    bool CommitProperty(Component& component, const std::string& field, Authoring::WriteDocument before);
    CommandCore::CommandResult Property(EntityHandle target, const std::string& component, const std::string& field, const std::string& value);
    CommandCore::CommandResult AddComponent(EntityHandle target, const std::string& type);
    CommandCore::CommandResult AddManagedScript(EntityHandle target, const std::string& type);
    CommandCore::CommandResult RemoveComponent(EntityHandle target, const std::string& component);
    CommandCore::CommandResult Select(Scene* scene, const std::vector<EntityHandle>& targets);
    CommandCore::CommandResult NavigateSelection(Scene* scene, int direction);
    CommandCore::CommandResult InstantiatePrefab(const std::string& prefab, const std::string& name);
    CommandCore::CommandResult InstantiatePrefab(Prefab* prefab, const std::string& name);
    CommandCore::CommandResult MaterialMode(const std::vector<std::shared_ptr<Material>>& materials, MaterialRenderingMode mode);
    CommandCore::CommandResult MaterialMode(EntityHandle target, MaterialRenderingMode mode);

    // PBR-W8 — 렌더러별 MaterialInstance override 를 헤드리스로 얹는다.
    //
    // ★ 그 전에는 `SetPropertyOverride` 에 닿는 표면이 GUI 인스펙터와 C# 스크립트
    //   뿐이었다. 게이트는 `--commandlet-script` 로만 움직이므로, "같은 `Material*`
    //   을 공유하는 렌더러 둘이 서로 다른 override 를 갖는" 상태를 **만들 수가
    //   없었다** — W8 이 밀봉 키를 주소에서 값으로 바꾼 그 결함을 자극할 fixture 가
    //   저장소에 없던 진짜 이유가 이것이다.
    //
    // `rendererIndex` 는 target 서브트리의 MeshRenderer 를 깊이 우선으로 센 색인이다
    // (MaterialMode(EntityHandle) 의 순회와 같은 규약). 오브젝트 id 로 주소를
    // 지정하려면 호출자가 먼저 트리를 걸어야 하는데, 모델 배치가 만드는 자식 이름은
    // 자산이 정하므로 색인이 게이트에 안정적이다.
    //
    // 값이 하나면 스칼라, 넷이면 baseColor 다. 둘 다 ShaderMeta 선언으로 검증되며
    // (MaterialScriptBinding), 이름·타입이 안 맞으면 조용히 삼키지 않고 실패한다.
    CommandCore::CommandResult MaterialOverride(EntityHandle target,
        int rendererIndex, const std::string& property,
        const std::vector<float>& values);
    CommandCore::CommandResult AnimatorParameter(EntityHandle target, const std::string& name, ValueType type);
    CommandCore::CommandResult AnimatorDefaultParameter(Animator& animator, ValueType type);
    CommandCore::CommandResult UndoRedo(bool redo);
}
