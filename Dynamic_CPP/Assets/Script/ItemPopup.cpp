#include "ItemPopup.h"
#include "Easing.h"
#include "pch.h"

void ItemPopup::Start()
{
    int childIdx = GetOwner()->m_childrenIndices[0];
    m_popupObj = GameObject::FindIndex(childIdx);
}

void ItemPopup::Update(float tick)
{
    if (m_active)
    {
        m_elapsed += tick;
        float t = std::clamp(m_elapsed / m_duration, 0.f, 1.f);
        if (t >= 1.f)
        {

        }
        else
        {
            float easedT = Easing::EaseOutBack(t);

            m_popupObj->m_transform.SetPosition(Mathf::Lerp(m_startPos, m_targetOffset, easedT));
            float scale = Mathf::Lerp(m_startScale, m_targetScale, easedT);
            m_popupObj->m_transform.SetScale({ scale, scale, scale });
        }
    }


}

