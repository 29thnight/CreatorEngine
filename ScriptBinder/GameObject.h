#pragma once
#include "Object.h"
#include "Component.h"
#include "Transform.h"
#include "GameObjectType.h"
#include "GameObjectIndex.h"
#include "PrefabOverride.h"
#include "ScenePhase.h"
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
	void RefreshComponentIdIndices();
	void AddChild(GameObject* _objcet);

	// 부모 인덱스와 Transform의 부모 ID를 함께 옮긴다.
	//
	// 둘은 항상 같아야 하는데 저장 위치가 갈라져 있어서, 한쪽만 갱신하는 실수가
	// 컴파일을 통과했다. Instantiate는 Transform에 자기 인덱스를 넘겼고, Scene의
	// 삭제·재매핑 경로 둘은 Transform을 아예 갱신하지 않아 부모 ID가 죽은 인덱스로
	// 남았다. 쓰기를 한 점으로 모으고 Transform::SetParentID를 닫아서, 쌍을 깨는
	// 코드가 애초에 컴파일되지 않게 한다.
	void SetParentIndex(Index parentIndex);

	// m_childrenIndices·m_rootIndex 쓰기의 정본 지점 (SceneGraphRedesignPlan §3
	// 트랙 E, E2). 최소 여섯 파일 스무 곳 가까이가 벡터를 직접 손으로 건드렸고,
	// 중복 검사가 있는 자리와 없는 자리가 섞여 있어 같은 자식이 두 번 들어가는
	// 경로가 있었다(Scene::AttachExistingGameObject 계열만 방어했다). AttachChildIndex는
	// 그 방어를 정본으로 삼아 항상 중복을 걸러낸다. 필드 자체는 아직 public이다 —
	// Dynamic_CPP 스크립트 다수가 읽기로 직접 접근해서, 봉인은 이 슬라이스의 범위 밖이다.
	void AttachChildIndex(Index childIndex);
	void DetachChildIndex(Index childIndex);
	void ClearChildren();
	// 배치 재매핑(로더 후속 배선)처럼 목록 전체를 통째로 교체해야 하는 자리 전용 —
	// 개별 부모-자식 연결에는 AttachChildIndex/DetachChildIndex를 쓴다.
	void SetChildrenIndices(std::vector<Index> children) { m_childrenIndices = std::move(children); }
	void SetRootIndex(Index rootIndex) { m_rootIndex = rootIndex; }
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

	// 이 인스턴스가 프리팹 원본 값을 지역적으로 덮어쓴 속성 목록 (SceneGraphRedesignPlan P1).
	// 프리팹 인스턴스가 아니면 항상 비어 있다. UpdateInstances는 이 목록에 있는 경로만
	// 프리팹의 새 값 적용에서 제외하고, 나머지는 그대로 받는다. 목록이 비어 있는
	// 구버전 씬/프리팹은 "오버라이드 없음"으로 읽힌다(예외 1의 읽기 호환).
	[[Property]]
	std::vector<PrefabOverride> m_prefabOverrides{};

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

	// 컴포넌트 타입 비트마스크 (SceneGraphRedesignPlan K1-a). 프로세스 로컬 순차
	// 인덱스(TypeTrait::ComponentTypeIndex) 기준이라 절대 직렬화하지 않는다 —
	// [[Property]]를 붙이지 않는다. "이 타입이 하나 이상 있는가"만 뜻한다
	// (AddComponentAllowMultiple로 여러 개 붙는 스크립트 쪽도 비트 하나로 접힌다).
	uint64_t m_componentTypeMask{ 0 };

	// 씬 그래프 상의 단계 (SceneGraphRedesignPlan §4 트랙 L1). 세션 로컬 런타임
	// 상태라 직렬화하지 않는다 — [[Property]]를 붙이지 않는다. 생성과 동시에
	// m_SceneObjects에 등록되는 현행 경로들(CreateGameObject 등) 때문에 기본값을
	// InScene으로 둔다 — Detached/Attached는 DDOL 이송 창(Scene::
	// DetachGameObjectHierarchy/AttachExistingGameObject)에서만 관측된다.
	ScenePhase m_scenePhase{ ScenePhase::InScene };

	// 컨테이너를 통째로 비우고 다시 채우는 경로(프리팹 갱신 등)를 위한 재구축.
	// m_components를 처음부터 훑어 마스크를 다시 세운다 — 그런 경로는 AddComponent를
	// 한 번씩 거치며 비트가 쌓이는 정상 경로를 우회하므로, 한 번에 맞춰야 한다.
	// 호출 지점은 소유 파일 밖에 있다(GameObject.cpp·PrefabUtility.cpp — 후속 배선).
	void RebuildComponentTypeMask()
	{
		m_componentTypeMask = 0;
		for (const auto& component : m_components)
		{
			if (!component) continue;

			const uint32_t index = TypeTrait::ComponentTypeIndex::Find(component->GetTypeID());
			if (index != TypeTrait::ComponentTypeIndex::kInvalid)
			{
				m_componentTypeMask |= (1ull << index);
			}
		}
	}

	Scene* m_ownerScene{ nullptr };
	Prefab* m_prefab{ nullptr };

	// 프리팹 원본 스냅샷 — 더 이상 오버라이드 판정의 정본이 아니다(그 자리는
	// m_prefabOverrides로 옮겼다, P1). 오버라이드 기록의 정본은 에디터가 프로퍼티를
	// 바꾸는 시점이어야 하지만 그 배선은 후속 슬라이스라, 그때까지 UpdateInstances가
	// m_prefabOverrides가 비어 있는 인스턴스를 만났을 때만 이 스냅샷과 현재 값을 1회
	// 비교해 목록을 시딩하는 마이그레이션 편의로만 쓴다. 비직렬화라 씬을 재로드하면
	// 비고, 그러면 시딩할 근거가 없어 "오버라이드 없음"으로 취급한다(예외 1과 같은 결과).
	YAML::Node m_prefabOriginal{};
	std::string m_removedSuffixNumberTag{};

	[[Property]]
	GameObjectType m_gameObjectType{ GameObjectType::Empty };
	[[Property]]
	bool m_isStatic{ false };
};

#include "GameObject.inl"


