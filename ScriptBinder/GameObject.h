#pragma once
#include "Object.h"
#include "Component.h"
#include "Transform.h"
#include "GameObjectType.h"
#include "GameObjectIndex.h"
#include "PrefabOverride.h"
#include "ScenePhase.h"
#include <vector>
#include <yaml-cpp/yaml.h>

class Scene;
class Bone;
class RenderScene;
class ModelLoader;
class Prefab;
class Entity : public Object, public std::enable_shared_from_this<Entity>
{
    public:
    using meta_identity = meta::identity_descriptor<Entity, Object>;
    static consteval auto reflect()
    {
        using Self = Entity;
        return meta::schema<Self>(
            meta::field<&Self::m_attachedSoketID>,
            // m_transform 필드 소멸(S1-b) — Transform이 Component로 승격되며
            // m_components 안의 컴포넌트 블록으로 직렬화된다. Entity 스키마에서
            // 빠지는 것이 의도된 형상 변경이다(리플렉션 골든 재기준선 필요).
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
            meta::field<&Self::m_isStatic>);
    }
public:
	using Index = GameObjectIndex;
	static constexpr Entity::Index INVALID_INDEX = std::numeric_limits<uint32_t>::max();
	// 씬 루트 오브젝트의 관례적 인덱스 (트랙 E3). 예전엔 AddChild의 루트
	// 폴백(Entity.cpp)이 이 값을 리터럴 0으로 썼다 — 이름을 붙여 "루트를
	// 가리키는 의도"임을 드러낸다. 통합 단계에서 Scene::GetRootObject()를
	// 신설해(Scene.h) AddChild/CreateGameObject/LoadGameObject의 루트 폴백이
	// 전부 이 상수 경유 접근자로 수렴했다.
	static constexpr Entity::Index kSceneRootIndex = 0;
	Entity();
	Entity(Scene* scene, std::string_view name, GameObjectType type, Entity::Index index, Entity::Index parentIndex);
	Entity(Scene* scene, size_t instanceID, std::string_view name, GameObjectType type, Entity::Index index, Entity::Index parentIndex);
	Entity(Entity&) = delete;
	Entity(Entity&&) noexcept = default;
	Entity& operator=(Entity&) = delete;
	~Entity() override = default;

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
	// K2 스테이지 A: m_components가 고유 소유(std::unique_ptr)로 바뀌며
	// shared_ptr 시그니처를 유지할 이유가 사라졌다 — 이 함수는 등록만 하고
	// 소유권을 갖지 않으므로 raw 포인터로 충분하다.
	void AttachComponentLifecycle(Component* component);

	// 공간 컴포넌트 부착의 단일 규칙 (S3) — 생성자 두 곳이 공유한다. 정의는
	// Entity.cpp(규칙의 근거 주석도 그쪽).
	void AttachSpatialComponent(GameObjectType type, Entity::Index parentIndex);

	// E7-c: GameObjectType은 생성 요청의 일회성 파라미터로만 남는다. 디스크에
	// 필드가 없는 신형 노드는 공간 컴포넌트 조합으로 생성 아키타입을 복원하고,
	// 구형 노드는 m_gameObjectType을 읽기 호환 입력으로만 받아들인다.
	static GameObjectType InferCreationType(const YAML::Node& node);

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
	void AddChild(Entity* _objcet);

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

	void RemoveComponentTypeID(uint32 typeID);

	// RemoveComponent(Meta::Type&)는 여기 없다 — 2025-05-06(Mono 통합, 4b250856)에
	// GetComponent(Meta::Type&)의 형제로 빈 몸통 스텁만 얹힌 채 들어왔고, Mono→CoreCLR
	// 이관과 K2(m_componentIds 소멸) 재작성을 그대로 통과하면서 단 한 번도 구현되지
	// 않았다. 호출부 전수(ScriptBinder·EngineEntry·EngineGUIWindow·RenderEngine·
	// Dynamic_CPP·Tools·C# ScriptCore/GameScripts/ClrHost 바인딩, 리플렉션 메서드
	// 등록 포함) 0건 확인 후 삭제(트랙 B). "타입으로 지워라"를 호출하면 아무 일도
	// 안 일어나는 채로 헤더에 남아 있는 것 자체가 함정이라, 구현을 채우기보다
	// 부재를 컴파일 에러로 드러내는 쪽을 택했다 — 필요해지면 GetComponent(const
	// Meta::Type&)·RemoveComponentTypeID(uint32)와 같은 자리에서 `type.typeID`를
	// RemoveComponentTypeID로 위임하는 한 줄로 복원하면 된다.

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

private:
	// Transform 컴포넌트 캐시 (S1-b: m_transform 값 멤버 소멸, 저장소는
	// m_components로 이동). 생성자에서 AddComponent<Transform>() 직후 채운다 —
	// GetComponent<Transform>() 특수화(Entity.inl)와 공개 접근자 Transform_()가
	// 여기를 읽어 FindComponentSlot 선형 탐색을 건너뛴다. UI는 의도적으로
	// Transform이 없으므로 nullptr가 정상 상태이고, Canvas는 Transform을 갖는다.
	Transform* m_pTransformComponent{ nullptr };

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
	static Entity* FindByNameInScene(Scene* scene, std::string_view name);
	static Entity* FindByIndexInScene(Scene* scene, Entity::Index index);
	static Entity* FindByInstanceIDInScene(Scene* scene, const HashedGuid& guid);
	static Entity* FindByAttachedIDInScene(Scene* scene, const HashedGuid& guid);

public:
    static Entity* Find(std::string_view name);
	static Entity* FindIndex(Entity::Index index);
	static Entity* FindInstanceID(const HashedGuid& guid);
	static Entity* FindAttachedID(const HashedGuid& guid);

	Entity* OwnerSceneFind(std::string_view name);
	Entity* OwnerSceneFindIndex(Entity::Index index);

	// Entity.inl의 자식 순회가 Scene 내부(m_SceneObjects)에 직접 손대지
	// 않게 하는 비템플릿 우회. 이것이 있어야 inl이 Scene.h를 include하지 않고,
	// Scene.h ↔ Entity.h 순환이 근본에서 끊긴다. 정의는 GameObject.cpp.
	// 범위·tombstone 검사는 Scene::TryGetGameObject에 위임한다(트랙 E3 —
	// 예전엔 무검사로 m_SceneObjects를 직접 인덱싱했다).
	Entity* SceneObjectAt(Entity::Index index) const;
	Entity* OwnerSceneFindInstanceID(const HashedGuid& guid);
	Entity* OwnerSceneFindAttachedID(const HashedGuid& guid);

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

	// Transform 컴포넌트 접근자 (S1-b: m_transform 값 멤버 소멸, 저장소는
	// m_components로 이동).
	//
	// 옛 `obj->Transform_().Foo()` 441곳(엔진 144·Dynamic_CPP 297)은 값 멤버가
	// 사라지며 전부 깨진다. 참조 멤버로 `m_transform`이라는 이름 자체를 살리는
	// 안은 기각했다 — 클래스 선언부 `GameObject(GameObject&&) noexcept = default;`를
	// 깨뜨린다는 것이 선행 조사로 확정됐다. 대신 이 접근자를 새 이름으로 둔다 —
	// GetComponent<Transform>()과 정확히 같은 캐시(m_pTransformComponent)를
	// 돌려준다(둘 다 O(1), FindComponentSlot 선형 탐색 없음). 옛 호출부는
	// `obj->Transform_().`을 `obj->Transform_().`로 고쳐야 한다 — 이 슬라이스가
	// 소유한 파일(Entity.cpp·Transform.cpp) 안은 이미 고쳤고, 나머지는
	// 이 슬라이스 최종 보고의 "통합 시 필요한 배선" 목록 참고.
	// ★ S3부터 널일 수 있다 — UI/Canvas는 Transform을 갖지 않는다.
	//
	// 참조를 돌려주는 표면이라 널이면 그 자리에서 죽는다. 그런데 이 접근자의
	// 호출부는 533곳이고, "UI에 도달하는 호출은 UIButton 한 곳뿐"이라는 판정은
	// 정적 분석이다 — 놓친 경로가 있으면 크래시로만 드러난다. 그래서 널을
	// **진단 가능한 사건**으로 바꾼다: 한 번만 로그를 남기고 공유 더미를 준다.
	// 더미에 쓴 값은 아무 데도 반영되지 않으므로 화면이 이상해지지만, 크래시와
	// 달리 로그가 호출부를 지목한다. S3가 안정되면 이 폴백을 제거하고 널 검사를
	// 호출부로 올린다(그때는 "UI에는 Transform이 없다"가 코드에 드러나야 한다).
	Transform& Transform_() const
	{
		if (nullptr == m_pTransformComponent) return MissingTransformFallback(this);
		return *m_pTransformComponent;
	}
	bool HasTransform() const { return nullptr != m_pTransformComponent; }

private:
	// 정의는 Entity.cpp — 로그 인프라(Debug)를 헤더로 끌어오지 않는다.
	static Transform& MissingTransformFallback(const Entity* who);
public:

	HashedGuid m_attachedSoketID{};
	Entity::Index m_index{ INVALID_INDEX };
	Entity::Index m_parentIndex{ INVALID_INDEX };
	//for bone update
	Entity::Index m_rootIndex{ 0 };
	uint32 m_collisionType = 0;
	FileGuid m_prefabFileGuid{ nullFileGuid };

	// 이 인스턴스가 프리팹 원본 값을 지역적으로 덮어쓴 속성 목록 (SceneGraphRedesignPlan P1).
	// 프리팹 인스턴스가 아니면 항상 비어 있다. UpdateInstances는 이 목록에 있는 경로만
	// 프리팹의 새 값 적용에서 제외하고, 나머지는 그대로 받는다. 목록이 비어 있는
	// 구버전 씬/프리팹은 "오버라이드 없음"으로 읽힌다(예외 1의 읽기 호환).
	std::vector<PrefabOverride> m_prefabOverrides{};

	std::vector<Entity::Index> m_childrenIndices;

public:
    HashingString m_tag{ "Untagged" };
    HashingString m_layer{ "Default" };

	// K2: m_componentIds(unordered_map<HashedGuid,size_t>) 소멸 — 이중 구조의
	// 절반이었다. 정본은 m_components 하나, 타입 조회는 FindComponentSlot(마스크
	// 선판정 + 선형 탐색)으로 대체됐다(SceneGraphRedesignPlan §4 트랙 K, K2).
	//
	// K2 스테이지 A: shared_ptr<Component> → std::unique_ptr<Component>.
	// 컴포넌트는 애초에 소유자가 Entity 하나뿐이었다 — 다른 시스템(Scene::
	// RegisterComponent, RenderScene/AnimationJob의 Animator* 등)은 전부 raw
	// 포인터로만 참조해 왔다(전제는 프레임 순서 불변식: GameLogic이 끝나야
	// DisableOrEnable→OnDestroy가 돈다 — AnimationJob 재적용 보고 참고). shared_ptr은
	// 그 사실을 감추고 있었을 뿐 실제로 공유된 적이 없다.
	//
	// 컴포넌트 소유·순서의 정본. K2 스테이지 B(SBO)는 폐기했다 — 되돌린 근거:
	//
	//   1. 이 컨테이너는 틱 경로에 없다. 프레임마다 도는 컴포넌트 순회는
	//      SystemSchedule의 평탄한 vector<Component*> 3벌이고, m_components를
	//      프레임마다 읽는 곳은 Scene::DestroyComponents의 정리 스윕뿐이다.
	//      SBO가 없앤 것(스폰 시 힙 할당)은 이 컨테이너의 지배 비용이 아니었다.
	//   2. 측정이 "perf 베이스라인 동등"이었다. 좋아진 것은 할당 횟수라는
	//      대리 지표뿐이고 시간은 어느 방향으로도 움직이지 않았다.
	//   3. 근거로 적혔던 "프리팹 89%가 0개"가 오측이었다(실측 36.2%. 씬 58.3%는
	//      맞다). 정정하면 무게중심이 "모든 오브젝트"가 아니라 "스폰 경로"로
	//      좁혀지는데, 그 경로의 이득은 측정된 바 없다.
	//   4. 인라인 용량 N은 이 프로젝트 에셋 분포에서 뽑은 값이라 다른 장르에
	//      그대로 서지 않는다. N은 타입 레이아웃의 일부라 나중에 못 바꾼다.
	//
	// 순서 보존 동적 배열이 정본인 이유(상용 엔진 대조): FindComponentSlot이
	// 인덱스 순서로 첫 매치를 돌려주고, AddComponentAllowMultiple로 같은 타입이
	// 여럿 붙으며, 컴포넌트가 YAML 시퀀스로 직렬화되어 프리팹 왕복 검사가
	// 게이트다 — 순서가 계약이다. Unreal의 TSet<UActorComponent*>는 순서를
	// 보장하지 않아 이 계약을 못 지키고, Godot식 이름 키 맵은 컴포넌트를
	// 타입으로만 찾는 이 엔진에 소비자가 없다. 조회는 m_componentTypeMask가
	// "없음"을 O(1)로 기각한 뒤에만 이 배열을 훑는다(FindComponentSlot).
	std::vector<std::unique_ptr<Component>> m_components{};

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
	// 호출 지점은 소유 파일 밖에 있다(Entity.cpp·PrefabUtility.cpp — 후속 배선).
	//
	// S1-b: m_pTransformComponent도 여기서 함께 재동기화한다. unique_ptr가
	// 벡터 안에서 재배치(swap/erase)되는 것만으로는 가리키는 힙 객체 주소가
	// 안 바뀌므로 캐시가 안전하지만, PrefabUtility 쪽처럼 컴포넌트를 파괴하고
	// "같은 타입을 다시 채우는" 경로가 있으면 옛 Transform 힙 객체는 죽고
	// 새 인스턴스가 그 자리를 대신한다 — 캐시를 안 갱신하면 댕글링이다. 이
	// 재동기화가 그 경로를 몰라도 안전하게 만드는 방어선이다.
	// ★ 정의를 Entity.cpp로 내렸다 — 본문의 dynamic_cast<Transform*>가
	// Transform의 완전 정의를 요구하는데, S1-b로 Transform이 Component 파생이
	// 되면서 Transform.h → Component.h → … → GameObject.h 순환이 생겼다.
	// 헤더에 두면 이 파일이 그 순환의 어느 지점에서 파싱되느냐에 따라 Transform이
	// 불완전해질 수 있고, 그건 include 순서 운에 기대는 것이다(실제로 새 시스템
	// 헤더가 추가되자 그 운이 깨져 C2680이 났다).
	void RebuildComponentTypeMask();

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

	bool m_isStatic{ false };
};

#include "GameObject.inl"


