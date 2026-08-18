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

	// ── 씬 그래프 6단계 생명주기 (SceneGraphRedesignPlan §4 트랙 L1) ──
	//
	// 기준점이 오브젝트가 아니라 컴포넌트다 — 옛 Awake는 "오브젝트가 태어남"이었지만
	// OnInitialized는 "이 컴포넌트가 초기화됨"이다. Existence(씬에 존재)와
	// Simulation(시뮬레이션 참가)이 별개 축이라 여섯이 필요하다.
	//
	// ★ L3 — 옛 Awake/Start/OnDestroy와 그 브리지를 걷어냈다.
	//   전환기에는 이 셋의 기본 구현이 옛 훅을 불러 주었고(`OnInitialized(){Awake();}`),
	//   그 덕에 옛 이름으로 쓰인 코드가 안 깨진 채 새 축이 먼저 섰다. 이제 살아있는
	//   소비자를 전부 새 이름으로 옮겼으므로 다리를 치운다 — 엔진 23종 77곳 +
	//   C# 14파일 18곳. 옛 이름이 남은 Dynamic_CPP 285곳은 **컴파일 대상이 아니다**
	//   (솔루션 7개 프로젝트에 없고 include하는 ModuleBehavior.h도 트리에 없다 —
	//   C++ 핫리로드 은퇴 후 남은 데이터 보존 폴더).
	//
	//   이관이 관측 가능한 것을 바꾸지 않은 이유 셋(전부 L1이 미리 맞춰 둔 것):
	//   ① 마스크가 OR 판정이라 옛 이름이든 새 이름이든 같은 비트가 선다
	//      (LifecycleRegistry::MaskOfType) → 스케줄링 불변.
	//   ② Scene이 이미 새 훅을 부르고 있었다(component->OnInitialized() 등).
	//   ③ LIFECYCLE_TRACE 라벨은 호출부의 고정 enum이라 훅 이름과 무관 →
	//      92사건 기준선 불변.
	virtual void OnInitialized() {}
	virtual void OnAddedToScene() {}
	virtual void OnBeginSimulation() {}
	virtual void OnEndSimulation() {}
	virtual void OnRemovingFromScene() {}
	virtual void OnUninitializing() {}

	// ── 활성/비활성 축 (6단계와 직교) ──
	//
	// 씬 페이즈와 무관하게 "지금 켜져 있는가"를 다룬다. 6단계에 대응물이 없어
	// 남는다 — 씬에 있고 시뮬레이션 중이면서도 꺼져 있을 수 있기 때문이다.
	virtual void OnEnable() {}
	virtual void OnDisable() {}

	// ── 틱 축 (트랙 C3가 시스템으로 이관 중) ──
	//
	// 네이티브 컴포넌트는 전용 시스템의 조밀 배열로 옮기고 있다(Animator·Decal·
	// Foliage·UITick·Sound·CharacterController 완료). 여기 가상 함수가 남아 있는
	// 이유는 아직 이관 안 된 넷(Light·Camera·Image·Canvas)과 스크립트 경로 때문이다.
	virtual void FixedUpdate(float fixedTick) {}
	virtual void Update(float tick) {}
	virtual void LateUpdate(float tick) {}

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
