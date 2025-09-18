#include "MovingUILayer.h"
#include "RectTransformComponent.h"
#include "pch.h"

void MovingUILayer::Start()
{
	m_movingTarget = GetComponent<RectTransformComponent>();
}

void MovingUILayer::Update(float tick)
{
	if (!m_movingTarget) return;

	m_elapsedTime += tick;

	if (!m_active && m_elapsedTime >= m_waitTick)
	{
		m_active = true;
		m_elapsedTime = 0;
	}

	if (m_active)
	{
		const float totalDist = 2.f * offset;
		const float duration = totalDist / m_movingSpeed;

		if (m_elapsedTime >= duration)
		{
			m_elapsedTime = 0;
			m_active = false;
		}

		const float phase = m_elapsedTime / duration;
		const float tri = 1.f - fabsf(2.f * phase - 1.f);

		//m_movingTarget->SetAnchoredPosition();
	}
}

