#include "BossHPBar.h"
#include "TagManager.h"
#include "RectTransformComponent.h"
#include "ImageComponent.h"
#include "Entity.h"
#include "TBoss1.h"
#include "pch.h"
void BossHPBar::Start()
{
    auto BossObj = GameObject::Find("Boss_edit");
    if (BossObj)
    {
        m_target = BossObj->weak_from_this();
        auto targetObj = m_target.lock();
        auto entityComp = targetObj->GetComponent<TBoss1>();
        if (entityComp)
        {
            m_entity = entityComp;
		}
    }

    m_rect = GetComponent<RectTransformComponent>();

    // HP�� �̹���
    if (!m_pOwner->m_childrenIndices.empty())
    {
        auto childIndex = m_pOwner->m_childrenIndices.front();
        if (auto childObj = GameObject::FindIndex(childIndex))
        {
            if (auto imageComp = childObj->GetComponent<ImageComponent>())
            {
                m_image = imageComp;
            }
        }
    }

    // �ʱ� ǥ�� ���� ����ȭ
    if (!m_target.expired() && m_image)
    {
        if (auto targetObj = m_target.lock())
        {
            if (auto entityComp = targetObj->GetComponent<Entity>())
            {
                m_currentHP = entityComp->m_currentHP;
                m_maxHP = entityComp->m_maxHP;
                if (m_maxHP > 0)
                {
                    float hpRatio = static_cast<float>(m_currentHP) / static_cast<float>(m_maxHP);
                    m_shownRatio = m_lerpFrom = m_lerpTo = std::clamp(hpRatio, 0.0f, 1.0f);
                    m_image->clipPercent = m_shownRatio;
                }
            }
        }
    }
}

void BossHPBar::Update(float tick)
{
    if (m_target.expired() || !m_rect || !m_image)
        return;

    if (!m_entity)
        return;

    m_currentHP = m_entity->Entity::m_currentHP;
    m_maxHP = m_entity->Entity::m_maxHP;
    if (m_maxHP <= 0)
        return;

    const float hpRatio = std::clamp(
        static_cast<float>(m_currentHP) / static_cast<float>(m_maxHP), 0.0f, 1.0f);

    // --- ��ǥ�� ��ȭ ����(�ε��Ҽ��� ����) ---
    const bool targetChanged = std::fabs(hpRatio - m_lastHpRatio) > kEps;
    m_lastHpRatio = hpRatio;

    // 1) HP ����(ȸ��): ��� �ݿ�, ���� �ߴ�
    if (hpRatio > m_shownRatio + kEps)
    {
        m_shownRatio = hpRatio;
        m_lerpFrom = hpRatio;
        m_lerpTo = hpRatio;
        m_lerpElapsed = 0.0f;
        m_isLerping = false;
    }
    // 2) HP ����: 0.5�� ���� (��ǥ�� �ٲ� ��쿡�� �����)
    else if (hpRatio < m_shownRatio - kEps)
    {
        if (!m_isLerping || (targetChanged && hpRatio < m_lerpTo - kEps))
        {
            m_lerpFrom = m_shownRatio;
            m_lerpTo = hpRatio;
            m_lerpElapsed = 0.0f;
            m_isLerping = true;
        }
    }

    // --- ���� ���� ---
    if (m_isLerping)
    {
        m_lerpElapsed = std::min(m_lerpElapsed + tick, kClipLerpDuration);
        float t = (kClipLerpDuration > 0.0f) ? (m_lerpElapsed / kClipLerpDuration) : 1.0f;
        t = std::clamp(t, 0.0f, 1.0f);

        m_shownRatio = m_lerpFrom + (m_lerpTo - m_lerpFrom) * t;

        // ���� �Ϸ� or ���� ���� �� ����
        if (t >= 1.0f || std::fabs(m_shownRatio - m_lerpTo) <= kEps)
        {
            m_isLerping = false;
            m_shownRatio = m_lerpTo;
            m_lerpFrom = m_shownRatio;
            m_lerpTo = m_shownRatio;   // ���� �����ӿ� ���� ���� ����
            m_lerpElapsed = 0.0f;
        }
    }

    // ���� �ݿ�(�ε��Ҽ��� ������ ��)
    if (std::fabs(m_image->clipPercent - m_shownRatio) > kEps)
        m_image->clipPercent = m_shownRatio;
}

