#include "UIButton.h"
#include "UITickSystem.h"
#include "RHI/ScreenSizedResource.h"
#include "InputManager.h"
#include "ImageComponent.h"
#include "RectTransformComponent.h"

void UIButton::TickInteraction(float deltaSecond)
{
	UpdateHitbox();
}

void UIButton::UpdateHitbox()
{
	m_hitbox = {};

    if (auto* rect = m_pOwner->GetComponent<RectTransformComponent>())
    {
		m_hitbox = rect->GetWorldRect();
    }
}

bool UIButton::CheckClick(math::vector2 _mousePos)
{
	math::vector2 gameViewPos = InputManagement->m_gameViewPos;
	math::vector2 gameViewSize = InputManagement->m_gameViewSize;
	// 화면 크기 버스에서 읽는다 - UIManager의 캔버스 크기 계산과 같은 출처다.
	math::vector2 screenSize = {
		static_cast<float>(ScreenResizeBus::Get().GetWidth()),
		static_cast<float>(ScreenResizeBus::Get().GetHeight())
	};
	if (gameViewSize.x <= 0.f || gameViewSize.y <= 0.f ||
		screenSize.x <= 0.f || screenSize.y <= 0.f)
	{
		isClick = false;
		return false;
	}
	// 입력은 Game View 좌상단 기준 0..W/0..H이고 RectTransform worldRect는 화면
	// 중심 기준 -W/2..W/2, -H/2..H/2다. 예전에는 이 원점 변환이 없어 보이는
	// 버튼보다 입력 판정이 화면 절반만큼 오른쪽/아래로 밀렸다.
	float localX = (_mousePos.x - gameViewPos.x) * (screenSize.x / gameViewSize.x) - screenSize.x * 0.5f;
	float localY = (_mousePos.y - gameViewPos.y) * (screenSize.y / gameViewSize.y) - screenSize.y * 0.5f;

	// RectTransform은 회전하지 않는 화면 사각형이다. 최대 모서리를 제외하는
	// half-open 규약으로 인접 버튼이 공유 모서리를 동시에 차지하지 않게 한다.
	isClick = math::contains(m_hitbox, math::vector2{ localX, localY });
	return isClick;
}

void UIButton::Click()
{
	// C++ 콜백이 없어도 세운다 — C# 쪽은 ConsumeClicked 폴링으로 받는다.
	m_wasClicked = true;

	if (m_clickFunction)
	{
		m_clickFunction();
	}
}

void UIButton::OnAddedToScene()
{
	UIComponent::OnAddedToScene();
	UITickSystems->RegisterButton(this);
}

void UIButton::OnRemovingFromScene()
{
	UITickSystems->UnregisterButton(this);
	UIComponent::OnRemovingFromScene();
}
