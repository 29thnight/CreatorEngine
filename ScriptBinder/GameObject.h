#pragma once
#include "Object.h"
#include "Component.h"
#include "Transform.h"
#include "GameObjectType.h"
#include "GameObjectIndex.h"
#include "PrefabOverride.h"
#include "ScenePhase.h"
// K2 스테이지 B: m_components의 실제 타입(InlineVector.h) — 헤더 자급자족을
// 위해 전이 include(TypeTrait.h 경유)에 기대지 않고 직접 문다.
#include "InlineVector.h"
#include <yaml-cpp/yaml.h>

class Scene;
class Bone;
class RenderScene;
class ModelLoader;
class Prefab;
class GameObject : public Object, public std::enable_shared_from_this<GameObject>
{
    public:
    using meta_identity = meta::identity_descriptor<GameObject, Object>;
    static consteval auto reflect()
    {
        using Self = GameObject;
        return meta::schema<Self>(
            meta::field<&Self::m_attachedSoketID>,
            meta::field<&Self::m_transform>,
            meta::field<&Self::m_index>,
            meta::field<&Self::m_parentIndex>,
            meta::field<&Self::m_rootIndex>,
            meta::field<&Self::m_collisionType>,
            meta::field<&Self::m_prefabFileGuid>,
            meta::field<&Self::m_prefabOverrides>,
            meta::field<&Self::m_childrenIndices>,
            meta::field<&Self::m_tag>,
            meta::field<&Self::m_layer>,
            meta::field<&Self::m_components>,
            meta::field<&Self::m_gameObjectType>,
            meta::field<&Self::m_isStatic>);
    }
public:
	using Index = GameObjectIndex;
	static constexpr GameObject::Index INVALID_INDEX = std::numeric_limits<uint32_t>::max();
	// 씬 루트 오브젝트의 관례적 인덱스 (트랙 E3). 예전엔 AddChild의 루트
	// 폴백(GameObject.cpp)이 이 값을 리터럴 0으로 썼다 — 이름을 붙여 "루트를
	// 가리키는 의도"임을 드러낸다. 통합 단계에서 Scene::GetRootObject()를
	// 신설해(Scene.h) AddChild/CreateGameObject/LoadGameObject의 루트 폴백이
	// 전부 이 상수 경유 접근자로 수렴했다.
	static constexpr GameObject::Index kSceneRootIndex = 0;
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
	//
	// K2 스테이지 A: m_components가 고유 소유(Managed::UniquePtr)로 바뀌며
	// shared_ptr 시그니처를 유지할 이유가 사라졌다 — 이 함수는 등록만 하고
	// 소유권을 갖지 않으므로 raw 포인터로 충분하다.
	void AttachComponentLifecycle(Component* component);

	// K2 스테이지 A: 반환이 shared_ptr<Component> → Component*로 바뀌었다.
	// m_components 자체가 고유 소유라 shared_ptr을 새로 만들 근거가 없다 —
	// 호출자는 이미 전부 raw 포인터로만 썼다(그린 상태 확인, GameObjectCommand.h
	// 등은 반환값을 쓰지 않는다).
	Component* AddComponent(const Meta::Type& type);

	// 같은 타입을 여러 개 붙일 수 있는 형태.
	//
	// 일반 AddComponent는 타입당 하나로 제한하고 기존 것을 돌려준다. 스크립트는 그 규칙을
	// 따를 수 없다 — 한 오브젝트에 스크립트를 여럿 붙이는 것이 보통이기 때문이다.
	// (관리 스크립트를 담는 ScriptComponent가 이쪽을 쓴다)
	Component* AddComponentAllowMultiple(const Meta::Type& type);
    Component* GetComponent(const Meta::Type& type);
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

	// typeID 하나로 컴포넌트를 찾는 공개 창구 (SceneGraphRedesignPlan §4 트랙 K, K2).
	//
	// m_componentIds(unordered_map<HashedGuid,size_t>)가 이중 구조의 절반이었다 —
	// m_components(벡터)와 따로 놀아서 늘 함께 갱신해야 했고, 실제로 RemoveComponent
	// 계열이 맵만 지우고 벡터는 그대로 두는 반쪽 상태로 어긋나 있었다(§1.1). 이제
	// 정본은 m_components 하나뿐이고, 조회는 FindComponentSlot(private)의 "마스크
	// 선판정 + 소배열 선형 탐색"으로 대체한다 — 오브젝트당 실사용 규모(0~6개,
	// 씬 58%·프리팹 89%가 0개)에서는 선형 탐색이 해시 버킷 조회보다 빠르고 캐시
	// 지역성도 낫다. Meta::Type 전체가 없어도(HashedGuid만 있어도) 쓸 수 있게 해서,
	// Component::GetComponent(형제 컴포넌트 조회)·에디터 리플렉션 드로어가 기존에
	// m_componentIds를 직접 뒤지던 자리를 이 함수 하나로 수렴시킨다(후속 배선 —
	// Component.cpp, EngineGUIWindow/ReflectionImGuiHelper.h는 소유 파일 밖).
	Component* FindComponent(const HashedGuid& typeID) const;

	bool IsStatic() const { return m_isStatic; }
	void SetStatic(bool isStatic) { m_isStatic = isStatic; }

	GameObjectType GetType() const { return m_gameObjectType; }

private:
	// 타입→슬롯 탐색의 단일 구현 (SceneGraphRedesignPlan §4 트랙 K, K2).
	//
	// K1-a 마스크가 이 typeID를 알고 있고 비트가 꺼져 있으면 "확실히 없음" —
	// 벡터를 훑지 않고 바로 끝낸다. 마스크가 모르는 typeID(미등록 타입)면 마스크로
	// 판단할 수 없으니 선형 탐색으로 내려간다. AddComponent<T>()의 중복 검사,
	// GetComponent<T>()·GetComponent(Meta::Type&)·FindComponent(HashedGuid) 전부
	// 이 하나로 수렴한다.
	static constexpr size_t kInvalidComponentSlot = static_cast<size_t>(-1);
	size_t FindComponentSlot(const HashedGuid& typeID) const;

	// 조회 9종 수렴의 단일 구현 (SceneGraphRedesignPlan §3 트랙 E, E3).
	//
	// 전역 Find*(활성 씬 기준)와 OwnerSceneFind*(m_ownerScene 기준)가 이름만
	// 다르고 몸통이 완전히 같았다(중복 본문 8벌). 씬 인자를 받는 이 넷으로
	// 수렴하고, 공개 API 8종은 각자의 씬을 넘겨 위임만 한다. 인덱스 조회는
	// Scene::TryGetGameObject에 맡기고, 이름·ID 검색은 기존 선형 탐색을
	// 유지하되 tombstone(nullptr) 슬롯 검사를 빠짐없이 한다.
	static GameObject* FindByNameInScene(Scene* scene, std::string_view name);
	static GameObject* FindByIndexInScene(Scene* scene, GameObject::Index index);
	static GameObject* FindByInstanceIDInScene(Scene* scene, const HashedGuid& guid);
	static GameObject* FindByAttachedIDInScene(Scene* scene, const HashedGuid& guid);

public:
    static GameObject* Find(std::string_view name);
	static GameObject* FindIndex(GameObject::Index index);
	static GameObject* FindInstanceID(const HashedGuid& guid);
	static GameObject* FindAttachedID(const HashedGuid& guid);

	GameObject* OwnerSceneFind(std::string_view name);
	GameObject* OwnerSceneFindIndex(GameObject::Index index);

	// GameObject.inl의 자식 순회가 Scene 내부(m_SceneObjects)에 직접 손대지
	// 않게 하는 비템플릿 우회. 이것이 있어야 inl이 Scene.h를 include하지 않고,
	// Scene.h ↔ GameObject.h 순환이 근본에서 끊긴다. 정의는 GameObject.cpp.
	// 범위·tombstone 검사는 Scene::TryGetGameObject에 위임한다(트랙 E3 —
	// 예전엔 무검사로 m_SceneObjects를 직접 인덱싱했다).
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

	HashedGuid m_attachedSoketID{};
	Transform m_transform{};
	GameObject::Index m_index{ INVALID_INDEX };
	GameObject::Index m_parentIndex{ INVALID_INDEX };
	//for bone update
	GameObject::Index m_rootIndex{ 0 };
	uint32 m_collisionType = 0;
	FileGuid m_prefabFileGuid{ nullFileGuid };

	// 이 인스턴스가 프리팹 원본 값을 지역적으로 덮어쓴 속성 목록 (SceneGraphRedesignPlan P1).
	// 프리팹 인스턴스가 아니면 항상 비어 있다. UpdateInstances는 이 목록에 있는 경로만
	// 프리팹의 새 값 적용에서 제외하고, 나머지는 그대로 받는다. 목록이 비어 있는
	// 구버전 씬/프리팹은 "오버라이드 없음"으로 읽힌다(예외 1의 읽기 호환).
	std::vector<PrefabOverride> m_prefabOverrides{};

	std::vector<GameObject::Index> m_childrenIndices;

public:
    HashingString m_tag{ "Untagged" };
    HashingString m_layer{ "Default" };

	// K2: m_componentIds(unordered_map<HashedGuid,size_t>) 소멸 — 이중 구조의
	// 절반이었다. 정본은 m_components 하나, 타입 조회는 FindComponentSlot(마스크
	// 선판정 + 선형 탐색)으로 대체됐다(SceneGraphRedesignPlan §4 트랙 K, K2).
	//
	// K2 스테이지 A: shared_ptr<Component> → Managed::UniquePtr<Component>.
	// 컴포넌트는 애초에 소유자가 GameObject 하나뿐이었다 — 다른 시스템(Scene::
	// RegisterComponent, RenderScene/AnimationJob의 Animator* 등)은 전부 raw
	// 포인터로만 참조해 왔다(전제는 프레임 순서 불변식: GameLogic이 끝나야
	// DisableOrEnable→OnDestroy가 돈다 — AnimationJob 재적용 보고 참고). shared_ptr은
	// 그 사실을 감추고 있었을 뿐 실제로 공유된 적이 없다.
	//
	// K2 스테이지 B: std::vector → InlineVector<Managed::UniquePtr<Component>, 4>
	// (SBO). 오브젝트당 실사용 규모가 0~6개(씬 58%·프리팹 89%가 0개 — 위
	// FindComponentSlot 주석의 실측)인데, std::vector는 첫 push_back마다 힙
	// 할당을 하나씩 물었다 — 대부분의 GameObject가 컴포넌트 하나만 지고도
	// 그 값을 담을 이유 없는 힙 조각을 냈다는 뜻이다. InlineVector는 N(=4)개까지
	// GameObject 안의 고정 버퍼에 두고, 그걸 넘어설 때만 힙으로 넘어간다(성장
	// 시점·재배치 시맨틱은 std::vector와 동일 — InlineVector.h 머리의 불변식
	// 참고). Component* 자체(다른 시스템이 캐시해 둔 raw 포인터)는 이 전환으로
	// 조금도 덜 안전해지지 않는다 — 옮겨지는 건 unique_ptr 핸들뿐이지 Component
	// 인스턴스가 아니다.
	InlineVector<Managed::UniquePtr<Component>, 4> m_components{};

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

	GameObjectType m_gameObjectType{ GameObjectType::Empty };
	bool m_isStatic{ false };
};

#include "GameObject.inl"


