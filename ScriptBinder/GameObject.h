#pragma once
#include "Object.h"
#include "Component.h"
#include "Transform.h"
#include "GameObjectType.h"
#include "GameObjectIndex.h"
#include "GameObject.generated.h"
#include <yaml-cpp/yaml.h>

class Scene;
class Bone;
class RenderScene;
class ModelLoader;
class Prefab;
class GameObject : public Object, public std::enable_shared_from_this<GameObject>
{
public:
	using Index = GameObjectIndex;
	static constexpr GameObject::Index INVALID_INDEX = std::numeric_limits<uint32_t>::max();
	enum class Type
	{
		Empty,
		Camera,
		Light,
		Mesh,
		Bone,
		UI,
		Canvas,
		TypeMax
	};
    ReflectGameObject
    [[Serializable(Inheritance:Object)]]
	GameObject();
	GameObject(Scene* scene, std::string_view name, GameObjectType type, GameObject::Index index, GameObject::Index parentIndex);
	GameObject(Scene* scene, size_t instanceID, std::string_view name, GameObjectType type, GameObject::Index index, GameObject::Index parentIndex);
	GameObject(GameObject&) = delete;
	GameObject(GameObject&&) noexcept = default;
	GameObject& operator=(GameObject&) = delete;
	~GameObject() override = default;

	HashingString GetHashedName() const { return m_name; }
	const std::string& RemoveSuffixNumberTag() const;

    void SetName(std::string_view name) { m_name = name.data(); }
    void SetTag(std::string_view tag);
    void SetLayer(std::string_view layer);

	virtual void Destroy() override final;

	// 생명주기 배선의 단일 지점 (PHASE 9-1).
	//
	// AddComponent 경로가 넷이라(템플릿 2 + 리플렉션 2) 전환 스위치를 각 자리에서
	// 보면 넷 중 하나를 빠뜨렸을 때 그 경로로 만든 컴포넌트만 조용히 틱을 못 받는다.
	// 한 곳으로 모아 그 실수를 구조적으로 막는다.
	void AttachComponentLifecycle(const std::shared_ptr<Component>& component);

	std::shared_ptr<Component> AddComponent(const Meta::Type& type);

	// 같은 타입을 여러 개 붙일 수 있는 형태.
	//
	// 일반 AddComponent는 타입당 하나로 제한하고 기존 것을 돌려준다. 스크립트는 그 규칙을
	// 따를 수 없다 — 한 오브젝트에 스크립트를 여럿 붙이는 것이 보통이기 때문이다.
	// (관리 스크립트를 담는 ScriptComponent가 이쪽을 쓴다)
	std::shared_ptr<Component> AddComponentAllowMultiple(const Meta::Type& type);
    std::shared_ptr<Component> GetComponent(const Meta::Type& type);
	std::shared_ptr<Component> GetComponentByTypeID(uint32 id);
	void RefreshComponentIdIndices();
	void AddChild(GameObject* _objcet);
	template<typename T>
	T* AddComponent();

	template<typename T, typename... Args>
	T* AddComponent(Args&&... args);

	template<typename T>
	T* GetComponent(uint32 id);

	template<typename T>
	T* GetComponent();

	template<typename T>
	T* GetComponentDynamicCast();

	template<typename T>
	std::vector<T*> GetComponentsInChildren();

	template<typename T>
	std::vector<T*> GetComponentsInchildrenDynamicCast();

	template<typename T>
	bool HasComponent();

	template<typename T>
	std::vector<T*> GetComponents();

	template<typename T>
	void RemoveComponent(T* component);

	void RemoveComponentIndex(uint32 id);
	void RemoveComponentTypeID(uint32 typeID);
	void RemoveComponent(Meta::Type& type);

	bool IsStatic() const { return m_isStatic; }
	void SetStatic(bool isStatic) { m_isStatic = isStatic; }

	GameObjectType GetType() const { return m_gameObjectType; }

    static GameObject* Find(std::string_view name);
	static GameObject* FindIndex(GameObject::Index index);
	static GameObject* FindInstanceID(const HashedGuid& guid);
	static GameObject* FindAttachedID(const HashedGuid& guid);

	GameObject* OwnerSceneFind(std::string_view name);
	GameObject* OwnerSceneFindIndex(GameObject::Index index);

	// GameObject.inl의 자식 순회가 Scene 내부(m_SceneObjects)에 직접 손대지
	// 않게 하는 비템플릿 우회. 이것이 있어야 inl이 Scene.h를 include하지 않고,
	// Scene.h ↔ GameObject.h 순환이 근본에서 끊긴다. 정의는 GameObject.cpp.
	GameObject* SceneObjectAt(GameObject::Index index) const;
	GameObject* OwnerSceneFindInstanceID(const HashedGuid& guid);
	GameObject* OwnerSceneFindAttachedID(const HashedGuid& guid);

	static inline bool IsValidIndex(Index index)
	{
		return index != INVALID_INDEX;
	}

	static inline bool IsInvalidIndex(Index index)
	{
		return index == INVALID_INDEX;
	}

	void SetEnabled(bool able) override final;
	void SetCollisionType();
	uint32 GetCollisionType() const { return m_collisionType; }
	Scene* GetScene() { return m_ownerScene; }

	[[Property]]
	HashedGuid m_attachedSoketID{};
    [[Property]]
	Transform m_transform{};
	[[Property]]
	GameObject::Index m_index{ INVALID_INDEX };
	[[Property]]
	GameObject::Index m_parentIndex{ INVALID_INDEX };
	//for bone update
    [[Property]]
	GameObject::Index m_rootIndex{ 0 };
	[[Property]]
	uint32 m_collisionType = 0;
	[[Property]]
	FileGuid m_prefabFileGuid{ nullFileGuid };
    [[Property]]
	std::vector<GameObject::Index> m_childrenIndices;

public:
    [[Property]]
    HashingString m_tag{ "Untagged" };
    [[Property]]
    HashingString m_layer{ "Default" };

	std::unordered_map<HashedGuid, size_t> m_componentIds{};
    [[Property]]
	std::vector<std::shared_ptr<Component>> m_components{};

	Scene* m_ownerScene{ nullptr };
	Prefab* m_prefab{ nullptr };
	YAML::Node m_prefabOriginal{};
	std::string m_removedSuffixNumberTag{};

	[[Property]]
	GameObjectType m_gameObjectType{ GameObjectType::Empty };
	[[Property]]
	bool m_isStatic{ false };
};

#include "GameObject.inl"


