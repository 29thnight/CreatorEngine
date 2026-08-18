#pragma once
#include "Object.h"
#include "TypeTrait.h"
#include "Reflection.hpp"
#include "MetaPolymorphic.h"


class GameObject;
class Transform;
class Component : public meta::identity<Component, Object>
{
   public:
   static consteval auto reflect()
   {
       return meta::schema<Self>(
           meta::field<&Self::m_FileID>);
   }
public:
	Component() = default;

	// ★ virtual인 이유 — S1-b(Transform 컴포넌트화)의 조용한 함정 차단.
	//
	// `Transform`도 **완전히 같은 시그니처**의 SetOwner(GameObject*)를 갖고 있다
	// (Transform.cpp — m_owner 대입 + m_parentID 동기 + SetDirty). Transform이
	// Component를 상속하는 순간, 둘 다 non-virtual이면 오버라이드가 아니라
	// **이름 은닉**이 되어 호출부의 정적 타입이 어느 쪽을 부를지 결정한다:
	//
	//   - 리플렉션 로드 경로(GameObject::AddComponent(const Meta::Type&) — 정적
	//     타입이 Component*)는 Component::SetOwner만 부른다 → Transform::m_owner가
	//     널로 남는다 → Transform::ResolveStore()가 영구 실패해 전부 LocalFallback으로
	//     떨어진다. **에러도 로그도 없다.**
	//   - 템플릿 경로(AddComponent<T>() — 정적 타입이 T*)는 Transform::SetOwner를
	//     부른다 → 정상.
	//
	// 즉 "코드로 만든 오브젝트"와 "파일에서 연 오브젝트"가 다르게 동작하게 된다.
	// virtual로 두면 두 경로가 같은 함수를 부른다. Component는 이미 vtable을
	// 가지므로(생명주기 훅 14종) 추가 비용은 엔트리 하나뿐이다.
	virtual void SetOwner(GameObject* owner);
	GameObject* GetOwner() const { return m_pOwner; }

	// ── 생명주기 훅 (PHASE 9-1) ──
	//
	// 예전에는 IRegistableEvent가 이 여덟 개를 들고 있었고, 컴포넌트는 Component와
	// RegistableEvent<T>를 함께 상속해야 훅을 받을 수 있었다. 이제 Component가 직접
	// 들고 있으므로 "컴포넌트라면 생명주기를 받는다"가 상속 선언이 아니라 타입 그
	// 자체에서 나온다.
	//
	// 두 경로가 이 하나의 가상 함수 집합을 공유한다 — 전환기의 델리게이트 경로도,
	// 새 레지스트리 경로도 여기를 부른다. 그래서 교체가 호출 대상을 바꾸지 않고
	// "누가 언제 부르는가"만 바꾼다(9-0 기준선이 그대로 유효한 이유다).
	virtual void Awake() {}
	virtual void OnEnable() {}
	virtual void Start() {}
	virtual void FixedUpdate(float fixedTick) {}
	virtual void Update(float tick) {}
	virtual void LateUpdate(float tick) {}
	virtual void OnDisable() {}
	virtual void OnDestroy() {}

	// ── 씬 그래프 6단계 생명주기 (SceneGraphRedesignPlan §4 트랙 L1) ──
	//
	// 기준점이 오브젝트에서 컴포넌트로 옮겨간다 — Awake는 "오브젝트가 태어남"이지만
	// OnInitialized는 "이 컴포넌트가 초기화됨"이다. 셋(OnInitialized·
	// OnBeginSimulation·OnUninitializing)은 대응하는 옛 훅이 있어 기본 구현이 그
	// 옛 훅을 부른다 — 위 여덟 훅으로 이미 이관된 컴포넌트 전부가 이 커밋으로
	// 깨지지 않는 이유다(전환기 브리지, 철거는 트랙 L3). 나머지 셋(OnAddedToScene·
	// OnEndSimulation·OnRemovingFromScene)은 옛 훅에 대응물이 없는 신설 축이라
	// 기본 구현이 비어 있다.
	virtual void OnInitialized() { Awake(); }
	virtual void OnAddedToScene() {}
	virtual void OnBeginSimulation() { Start(); }
	virtual void OnEndSimulation() {}
	virtual void OnRemovingFromScene() {}
	virtual void OnUninitializing() { OnDestroy(); }

	// 생명주기 진행 상태 (PHASE 9-1).
	//
	// Awake·Start는 컴포넌트당 한 번뿐이라는 것이 계약이다. 그런데 등록은 한 번이
	// 아니다 — DDOL 오브젝트는 씬을 건널 때마다 새 씬에 다시 등록되고, 경로 전환도
	// 재등록을 부른다. 상태를 컴포넌트가 들고 있지 않으면 그때마다 Awake가 다시 돈다.
	//
	// (델리게이트 경로는 같은 상태를 IRegistableEvent가 들고 있었다. 훅이 Component로
	//  온 이상 상태도 여기 있어야 짝이 맞는다.)
	enum LifecycleState : uint8_t
	{
		State_AwakeCalled = 1u << 0,
		State_StartCalled = 1u << 1,
	};

	bool HasLifecycleState(uint8_t bit) const noexcept { return 0 != (m_lifecycleState & bit); }
	void MarkLifecycleState(uint8_t bit) noexcept { m_lifecycleState |= bit; }

	// 활성 전이에서 OnEnable/OnDisable을 부른다 (PHASE 9-2).
	//
	// 예전에는 Scene이 매 프레임 OnEnableEvent/OnDisableEvent를 브로드캐스트하고,
	// 구독한 람다가 "이전 상태와 달라졌나"를 각자 검사했다. 즉 상태가 안 바뀐
	// 프레임에도 전체를 훑었다 — 그런데 이 훅을 구현한 네이티브 컴포넌트는 0개라
	// 그 스캔은 매 프레임 아무 일도 하지 않고 돌기만 했다.
	//
	// 전이는 SetEnabled에서만 일어난다. 그 자리에서 직접 부르면 스캔이 통째로 사라지고,
	// 호출 시점도 '다음 브로드캐스트'가 아니라 '바뀐 그 순간'으로 명확해진다.
	void SetEnabled(bool able) override
	{
		const bool wasEnabled = IsEnabled();
		Object::SetEnabled(able);
		if (wasEnabled == able) return;

		if (able) OnEnable();
		else      OnDisable();
	}

	//template<typename T>
	//T& GetComponent();

	template<typename T>
	T* GetComponent();

	template<typename T>
	T* GetComponentDynamicCast();
	//Transform�� ��쿡�� GetComponent<Transform>()�� ���
	Component& GetComponent(HashedGuid typeof);

protected:
	uint8_t			m_lifecycleState{ 0 };
	GameObject*		m_pOwner{};
	Transform*		m_pTransform{ nullptr };
	FileGuid m_FileID{};
};

#include "Component.inl"
