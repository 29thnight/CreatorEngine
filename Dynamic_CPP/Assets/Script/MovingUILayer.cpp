#include "MovingUILayer.h"
#include "PlayerSelector.h"
#include "RectTransformComponent.h"
#include "Core.Mathf.h"
#include "pch.h"

void MovingUILayer::Start()
{
	if (!iconRect) iconRect = m_pOwner->GetComponent<RectTransformComponent>();

	// PlayerSelector 구독
	if (auto* gm = GameObject::Find("GameManager"))
	{
		m_selector = gm->GetComponent<PlayerSelector>();
		if (m_selector) 
		{
			m_selector->RegisterObserver(this);
			// Start 시점 초기 위치를 현재 위치로 지정
			if (iconRect) 
			{
				neutralPos = iconRect->GetAnchoredPosition();
			}
			m_curSlot = SelectorSlot::Neutral;
		}
	}

	GameObject* lefttargetObj = nullptr;
	GameObject* righttargetObj = nullptr;
	RectTransformComponent* LPosRect = nullptr;
	RectTransformComponent* RPosRect = nullptr;

	if (lefttargetObj = GameObject::Find("readyButtonSlot (0)"))
	{
		LPosRect = lefttargetObj->GetComponent<RectTransformComponent>();
		if (LPosRect) leftPos = LPosRect->GetAnchoredPosition();
	}

	if (righttargetObj = GameObject::Find("readyButtonSlot (1)"))
	{
		RPosRect = righttargetObj->GetComponent<RectTransformComponent>();
		if (RPosRect) rightPos = RPosRect->GetAnchoredPosition();
	}
}

void MovingUILayer::Update(float tick)
{
	if (!m_animating || !iconRect) return;

	m_elapsed += tick;
	float t = std::clamp(m_elapsed / std::max(duration, 0.0001f), 0.f, 1.f);

	float alpha = useEaseOutQuad ? Mathf::Easing::EaseOutQuad(t) : t;

	const float x = Mathf::Lerp(m_startPos.x, m_targetPos.x, alpha);
	const float y = Mathf::Lerp(m_startPos.y, m_targetPos.y, alpha);
	iconRect->SetAnchoredPosition({ x, y });

	if (t >= 1.f) {
		m_animating = false;
		iconRect->SetAnchoredPosition(m_targetPos);
	}
}

void MovingUILayer::OnDestroy()
{
	if (m_selector)
	{
		m_selector->RegisterObserver(this);
	}
}

void MovingUILayer::OnSelectorChanged(const SelectorState& state)
{
	if (!iconRect) return;
	if (state.playerIndex != playerIndex) return;

	// 목표 좌표 결정
	const Mathf::Vector2 target = SlotToPos(state.slot);

	// 동일한 목표면 무시(불필요한 트윈 방지)
	if (target == m_targetPos && !m_animating) return;

	// 트윈 초기화
	m_startPos = iconRect->GetAnchoredPosition();
	m_targetPos = target;
	m_elapsed = 0.f;
	m_animating = true;
	m_curSlot = state.slot;
}

