#include "ItemPopup.h"
#include "Player.h"
#include "pch.h"

void ItemPopup::Start()
{
    if(!GetOwner()->m_childrenIndices.empty())
    {
        int childIdx = GetOwner()->m_childrenIndices[0];
        m_popupObj = GameObject::FindIndex(childIdx);
    }
}
//TODO : 조금만 더 생각해보자
// -> 텔레포트되었을떄 Exit이 보장되는지가 확실하지 않음.
void ItemPopup::OnTriggerEnter(const Collision& collision)
{
    if(collision.otherObj->HasComponent<Player>())
    {
        ++m_enterCount;
    }
}

void ItemPopup::OnTriggerExit(const Collision& collision)
{
    if (collision.otherObj->HasComponent<Player>())
    {
        --m_enterCount;

		m_enterCount = std::max(0, m_enterCount);
    }
}

void ItemPopup::Update(float tick)
{
	using namespace Mathf;
    if (!m_popupObj) return;

    if (0 >= m_enterCount)
    {
        m_active = false;
    }
    else
    {
		m_active = true;
    }

    if (m_active)
    {
        // 올라오는 트윈
        if (!m_bobbing)
        {
            m_elapsed += tick;
            float t = std::clamp(m_elapsed / m_duration, 0.f, 1.f);

            if (t < 1.f)
            {
                float easedT = Easing::EaseOutBack(t);

                m_popupObj->m_transform.SetPosition(
                    Mathf::Lerp(m_startPos, m_targetOffset, easedT));

                float scale = Mathf::Lerp(m_startScale, m_targetScale, easedT);
                m_popupObj->m_transform.SetScale({ scale, scale, scale });
            }
            else
            {
                // 트윈 완료 → 보빙 시작
                m_popupObj->m_transform.SetPosition(m_targetOffset);
                m_popupObj->m_transform.SetScale({ m_targetScale, m_targetScale, m_targetScale });

                m_bobbing = true;
                m_bobTime = 0.f;
            }
        }
        else
        {
            // 보빙 단계
            m_bobTime += tick;
            float amp = (m_bobDamping > 0.f)
                ? (m_bobAmp0 * std::exp(-m_bobDamping * m_bobTime))
                : m_bobAmp0;

            float bob = amp * std::sinf(6.28318f * m_bobFreq * m_bobTime + m_bobPhase);

            Mathf::Vector3 pos = m_targetOffset + Mathf::Vector3(0.f, 0.f, bob);
            m_popupObj->m_transform.SetPosition(pos);
            m_popupObj->m_transform.SetScale({ m_targetScale, m_targetScale, m_targetScale });
        }
    }
    else
    {
        // 내려가는 트윈 (보빙 중지)
        m_bobbing = false;
        m_elapsed -= tick; // 거꾸로 줄여나감
        m_elapsed = std::max(0.f, m_elapsed);

        float t = std::clamp(m_elapsed / m_duration, 0.f, 1.f);
        float easedT = Easing::EaseOutBack(t);

        // 반대로 보간: targetOffset → startPos, targetScale → startScale
        m_popupObj->m_transform.SetPosition(
            Mathf::Lerp(m_startPos, m_targetOffset, easedT));

        float scale = Mathf::Lerp(m_startScale, m_targetScale, easedT);
        m_popupObj->m_transform.SetScale({ scale, scale, scale });
    }
}

