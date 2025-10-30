#include "UIHPObserver.h"
#include "Entity.h"
#include "ImageComponent.h"
#include "pch.h"
void UIHPObserver::Start()
{
	m_image = GetComponent<ImageComponent>();
}

void UIHPObserver::Update(float tick)
{
    if (!m_entity || !m_image) return;

    m_currentHP = m_entity->m_currentHP;
    m_maxHP = m_entity->m_maxHP;

    if (m_maxHP <= 0) {
        m_image->color = { 1.f, 1.f, 1.f, 1.f };
        m_blinkTimer = 0.f;
        return;
    }

    const float hpPercent = static_cast<float>(m_currentHP) / static_cast<float>(m_maxHP);

    if (hpPercent <= m_warningPersent)
    {
        // 토글(스퀘어) 깜빡임
        m_blinkTimer += tick;
        float phase = (m_blinkPeriod > 0.f)
            ? fmodf(m_blinkTimer, m_blinkPeriod) / m_blinkPeriod
            : 0.f;

        const bool showWarning = (phase < m_onRatio);
        m_image->color = showWarning ? m_warningColor
            : Mathf::Color4{ 1.f, 1.f, 1.f, 1.f };
    }
    else
    {
        // 경고 해제
        m_image->color = { 1.f, 1.f, 1.f, 1.f };
        m_blinkTimer = 0.f; // 다음 경고 시 위상 초기화
    }
}

