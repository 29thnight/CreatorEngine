#pragma once
#include "Object.h"
#include "TypeTrait.h"
#include "Reflection.hpp"
#include "ManagedHeapObject.h"
#include "Component.generated.h"


class GameObject;
class Transform;
class Component : public Object
{
public:
   ReflectComponent
    [[Serializable(Inheritance:Object)]]
	GENERATED_BODY(Component)

	void SetOwner(GameObject* owner);
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
	[[Property]]
	FileGuid m_FileID{};
};

#include "Component.inl"
