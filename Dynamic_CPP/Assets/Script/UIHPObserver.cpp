#include "UIHPObserver.h"
#include "Entity.h"
#include "ImageComponent.h"
#include "GameManager.h"
#include "pch.h"
void UIHPObserver::Start()
{
	m_image = GetComponent<ImageComponent>();

    auto gmobj = GameObject::Find("GameManager");
    if (gmobj)
    {
        GM = gmobj->GetComponent<GameManager>();
	}
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
	// 게임 오버 시 회색 처리
    if (GM && GM->m_isGameOver)
    {
        m_image->color = { 0.43f, 0.43f, 0.43f, 1.f };
        m_blinkTimer = 0.f;
		return;
    }
	// HP 비율 계산
    const float hpPercent = static_cast<float>(m_currentHP) / static_cast<float>(m_maxHP);
	// 경고 상태 체크
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

