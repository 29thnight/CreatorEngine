#pragma once
#include <mathematics/vector2.hpp>
#include "../Utility_Framework/Core.Minimal.h"
#include <mathematics/rect.hpp>
#include "Component.h"
#include "UIManager.h"
#include "UIComponent.h"

class UIButton : public meta::identity<UIButton, UIComponent>
{
   public:
   static consteval auto reflect()
   {
       return meta::schema<Self>(
           meta::method<&Self::Click>);
   }
public:
	UIButton() = default;
	
	// C3 — 가상 Update 오버라이드를 버리고 UITickSystem이 부르는 비가상 진입점으로.
	void TickInteraction(float deltaSecond);
	// C3 — 시스템 등록·해지는 6단계 훅에 건다. Awake/OnDestroy가 아닌 이유는
	// DDOL 오브젝트가 씬을 건널 때 Awake가 다시 불리지 않아 등록부에서 영구
	// 이탈하기 때문이다(UITickSystem.h 주석의 근거와 동일).
	void OnAddedToScene() override;
	void OnRemovingFromScene() override;

	void SetClickFunction(std::function<void()> func)
	{
		m_clickFunction = func;
	}
	void UpdateHitbox();
	bool CheckClick(math::vector2 _mousePos);
	void SetFunction(std::string& funName,float key,std::function<void()> func) { m_clickFunction = func;}
	void Click();

	// 클릭 판정에 쓰이는 사각형. 렌더 좌표와 입력 좌표가 같은 사각형을 가리키는지
	// 검증하기 위해 노출한다 — 둘이 어긋나면 "보이는 곳과 눌리는 곳"이 달라진다(PHASE 7-7).
	const math::rect& GetHitbox() const { return m_hitbox; }

	// C# 폴링용. Click()이 세우고, 스크립트가 틱에서 읽으면 내려간다.
	// 콜백 델리게이트를 경계 너머로 넘기는 대신 "틱당 1회" 규약대로 폴링한다 —
	// 클릭은 프레임당 최대 1회라 래치 하나로 유실 없이 전달된다.
	bool ConsumeClicked()
	{
		const bool wasClicked = m_wasClicked;
		m_wasClicked = false;
		return wasClicked;
	}

private:
	math::rect m_hitbox{};
	std::function<void()> m_clickFunction;
	bool m_wasClicked = false;

public:
	bool isClick = false;
};

