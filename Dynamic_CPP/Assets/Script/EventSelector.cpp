#include "EventSelector.h"
#include "GameInstance.h"
#include "EventManager.h"
#include "TextComponent.h"
#include "ImageComponent.h"
#include "RectTransformComponent.h"
#include "EffectComponent.h"
#include "StringHelper.h"
#include "pch.h"

namespace
{
    constexpr float kMinDuration = 1e-4f;
}

void EventSelector::Start()
{
	m_textComponent = GetOwner()->GetComponent<TextComponent>();
	m_textRectTransform = GetOwner()->GetComponent<RectTransformComponent>();
    //TODO : 찾아라 드래곤볼 해야함.
    //m_imageComponent = GetOwner()->GetComponent<ImageComponent>();
    //m_rectTransform = GetOwner()->GetComponent<RectTransformComponent>();
    GameObject* parentObj = GameObject::FindIndex(GetOwner()->m_parentIndex);
    GameObject* grandParentObj{};
    if (parentObj)
    {
		m_iconComponent = parentObj->GetComponent<ImageComponent>();
        grandParentObj = GameObject::FindIndex(parentObj->m_parentIndex);
	}
    //최상위 이미지 컴포넌트
    if (grandParentObj)
    {
        m_imageComponent = grandParentObj->GetComponent<ImageComponent>();
        m_rectTransform = grandParentObj->GetComponent<RectTransformComponent>();
	}

    if(!GetOwner()->m_childrenIndices.empty())
    {
        int childInded = GetOwner()->m_childrenIndices[0];
        int chkIconIndex{};
        GameObject* chkboxObj = GameObject::FindIndex(childInded);
        if (chkboxObj)
        {
            m_chkboxIconComponent = chkboxObj->GetComponent<ImageComponent>();
			m_chkboxRectTransform = chkboxObj->GetComponent<RectTransformComponent>();
			if (!chkboxObj->m_childrenIndices.empty())
            {
                chkIconIndex = chkboxObj->m_childrenIndices[0];
            }
        }

        if (chkIconIndex != GameObject::INVALID_INDEX)
        {
            GameObject* chkIconObj = GameObject::FindIndex(chkIconIndex);
            if (chkIconObj)
            {
				m_chkIconRectTransform = chkIconObj->GetComponent<RectTransformComponent>();
                m_chkIconComponent = chkIconObj->GetComponent<ImageComponent>();
            }
        }
    }

    if (m_textComponent)
    {
        m_baseTextColor     = m_textComponent->GetColor();
        m_hasBaseTextColor  = true;
        m_textComponent->SetMessage(m_defaultMessage);
    }

    if (m_imageComponent)
    {
        m_baseImageColor    = m_imageComponent->color;
        m_hasBaseImageColor = true;
    }

    if (m_rectTransform)
    {
        m_visiblePosition   = m_rectTransform->GetAnchoredPosition();
        m_hiddenPosition    = m_visiblePosition;
        m_hiddenPosition.x -= m_hiddenOffset;
        m_animStartPos      = m_hiddenPosition;
        m_animTargetPos     = m_visiblePosition;
        m_rectTransform->SetAnchoredPosition(m_hiddenPosition);
		m_rectTransform->SetSizeDelta(m_bgSize);
        m_initializedPositions = true;
    }

    SetVisualAlpha(0.f);

    m_state = UiState::Hidden;
    m_stateTimer = 0.f;
    m_effectPlayed = false;
    m_currentEventId.reset();
    m_pendingEventId.reset();
    m_currentMessage.clear();
    m_pendingMessage.clear();
}

void EventSelector::Update(float tick)
{
    auto* instance = GameInstance::GetInstance();
    EventManager* eventMgr = instance ? instance->GetActiveEventManager() : nullptr;

    std::optional<int> activeId;
    std::string activeText = m_defaultMessage;

    if (eventMgr)
    {
        const auto activeIds = eventMgr->GetActiveEventIds();
        if (!activeIds.empty())
        {
            activeId = activeIds.front();
            if (const auto* def = eventMgr->GetEventDefinition(*activeId))
            {
                if (!def->uiText.empty())
                {
                    activeText = def->uiText;
                }
                else if (!def->name.empty())
                {
                    activeText = def->name;
                }
                else
                {
                    activeText = std::string("Event ") + std::to_string(*activeId);
                }
            }
            else
            {
                activeText = std::string("Event ") + std::to_string(*activeId);
            }
        }
    }

    HandleActiveEvent(activeId, activeText);
    UpdateState(tick);
    if (m_textComponent)
    {
        auto vec2 = m_textComponent->m_textMeasureSize;
        if (m_chkboxRectTransform && m_textRectTransform)
        {
            auto chkboxPos = m_chkboxRectTransform->GetAnchoredPosition();
            float textPosX = m_textRectTransform->GetAnchoredPosition().x;
            float textRectSizeX = m_textRectTransform->GetSizeDelta().x;
            float textMeasureX = vec2.x;
            float iconSizeX = m_chkboxIconComponent->uiinfo.size.x;

            float chkboxPosX = textPosX - (textRectSizeX / 2.f) + textMeasureX + (iconSizeX / 2.f) + 15.f;

            chkboxPosX = std::clamp(chkboxPosX, 350.f, 610.f);

            m_chkboxRectTransform->SetAnchoredPosition({ chkboxPosX, chkboxPos.y });
        }
    }
}

void EventSelector::HandleActiveEvent(const std::optional<int>& activeId, const std::string& activeText)
{
    if (m_state == UiState::Completing)
    {
        if (activeId)
        {
            if (!m_pendingEventId || m_pendingEventId != activeId)
            {
                m_pendingEventId = activeId;
                m_pendingMessage = activeText;
            }
        }
        else
        {
            m_pendingEventId.reset();
            m_pendingMessage.clear();
        }
        return;
    }

    if (m_currentEventId)
    {
        if (!activeId || activeId != m_currentEventId)
        {
            BeginCompletion();
            if (activeId)
            {
                m_pendingEventId = activeId;
                m_pendingMessage = activeText;
            }
            else
            {
                m_pendingEventId.reset();
                m_pendingMessage.clear();
            }
        }
        else if (m_state == UiState::Displaying && activeText != m_currentMessage)
        {
            m_currentMessage = activeText;
            if (m_textComponent)
            {
                m_textComponent->SetMessage(m_currentMessage);
            }
        }
        return;
    }

    if (activeId)
    {
        if (m_state == UiState::Hidden)
        {
            BeginEnter(*activeId, activeText);
        }
        else
        {
            m_pendingEventId = activeId;
            m_pendingMessage = activeText;
        }
    }
    else if (m_state == UiState::Hidden && m_textComponent)
    {
        m_textComponent->SetMessage(m_defaultMessage);
    }
}

void EventSelector::BeginEnter(int eventId, std::string message)
{
    m_currentEventId = eventId;
    m_currentMessage = std::move(message);
    m_stateTimer = 0.f;
    m_effectPlayed = false;
    m_pendingEventId.reset();
    m_pendingMessage.clear();

    if (m_textComponent)
    {
        m_textComponent->SetMessage(m_currentMessage);
    }

    if (!m_effectPlayed && m_chkIconComponent)
    {
        m_chkIconComponent->SetEnabled(false);
        m_effectPlayed = false;
    }

    if (m_rectTransform && m_initializedPositions)
    {
        m_animStartPos = m_hiddenPosition;
        m_animTargetPos = m_visiblePosition;
        m_rectTransform->SetAnchoredPosition(m_hiddenPosition);
        m_state = UiState::Entering;
        SetVisualAlpha(0.f);
    }
    else
    {
        m_state = UiState::Displaying;
        SetVisualAlpha(1.f);
    }
}

void EventSelector::BeginCompletion()
{
    if (!m_currentEventId)
    {
        return;
    }

    if (!m_rectTransform || !m_initializedPositions)
    {
        FinishCompletion();
        return;
    }

    if (m_state == UiState::Completing)
    {
        return;
    }

    m_state = UiState::Completing;
    m_stateTimer = 0.f;
    m_effectPlayed = false;
    m_animStartPos = m_rectTransform->GetAnchoredPosition();
    m_animTargetPos = m_hiddenPosition;
}

void EventSelector::FinishCompletion()
{
    if (m_rectTransform && m_initializedPositions)
    {
        m_rectTransform->SetAnchoredPosition(m_hiddenPosition);
    }

    SetVisualAlpha(0.f);
    m_state = UiState::Hidden;
    m_stateTimer = 0.f;
    m_effectPlayed = false;
    m_currentEventId.reset();
    m_currentMessage.clear();

    if (m_textComponent)
    {
        m_textComponent->SetMessage(m_defaultMessage);
    }

    if (m_pendingEventId)
    {
        const int nextId = *m_pendingEventId;
        auto nextMessage = std::move(m_pendingMessage);
        m_pendingEventId.reset();
        m_pendingMessage.clear();
        BeginEnter(nextId, std::move(nextMessage));
    }
    else
    {
        m_pendingMessage.clear();
    }
}

void EventSelector::UpdateState(float tick)
{
    switch (m_state)
    {
    case UiState::Hidden:
        if (m_pendingEventId)
        {
            const int nextId = *m_pendingEventId;
            auto nextMessage = std::move(m_pendingMessage);
            m_pendingEventId.reset();
            m_pendingMessage.clear();
            BeginEnter(nextId, std::move(nextMessage));
            if (m_chkIconComponent)
            {
                m_chkIconComponent->SetEnabled(false);
            }
        }
        break;
    case UiState::Entering:
    {
        const float duration = std::max(m_slideInDuration, kMinDuration);
        m_stateTimer += tick;
        const float normalized = std::clamp(m_stateTimer / duration, 0.f, 1.f);
        const float eased = Mathf::Easing::EaseOutQuad(normalized);
        ApplyAnimation(eased);
        SetVisualAlpha(eased);
        if (normalized >= 1.f)
        {
            m_state = UiState::Displaying;
            m_stateTimer = 0.f;
            SetVisualAlpha(1.f);
            if (m_rectTransform && m_initializedPositions)
            {
                m_rectTransform->SetAnchoredPosition(m_visiblePosition);
                if (m_textComponent)
                {
                    auto vec2 = m_textComponent->m_textMeasureSize;
                    if (0 < vec2.x)
                    {
						float doubleX = vec2.x * 2.f;
						doubleX = std::clamp(doubleX, 400.f, 900.f);
						m_rectTransform->SetSizeDelta({ doubleX + 20.f, m_bgSize.y });
                    }
                }
            }
        }
        break;
    }
    case UiState::Displaying:
        if (m_pendingEventId && m_currentEventId && m_pendingEventId != m_currentEventId)
        {
            BeginCompletion();
        }
        break;
    case UiState::Completing:
    {
        m_effectTimer += tick;
        if (!m_effectPlayed && m_chkIconComponent)
        {
            m_chkIconComponent->SetEnabled(true);
        }

        if (m_effectTimer >= 0.8f)
        {
            m_effectPlayed = true;
        }

        if (m_effectPlayed)
        {
            m_effectTimer = 0.f;
            const float duration = std::max(m_slideOutDuration, kMinDuration);
            m_stateTimer += tick;
            const float normalized = std::clamp(m_stateTimer / duration, 0.f, 1.f);
            const float eased = Mathf::Easing::EaseInQuad(normalized);
            ApplyAnimation(eased);
            SetVisualAlpha(1.f - eased);

            if (normalized >= 1.f)
            {
                FinishCompletion();
            }
        }
        break;
    }
    default:
        break;
    }
}

void EventSelector::ApplyAnimation(float alpha)
{
    if (!m_rectTransform || !m_initializedPositions)
    {
        return;
    }

    const float clampedAlpha = std::clamp(alpha, 0.f, 1.f);
    const auto pos = Mathf::Lerp(m_animStartPos, m_animTargetPos, clampedAlpha);
    m_rectTransform->SetAnchoredPosition(pos);
}

void EventSelector::SetVisualAlpha(float alpha)
{
    const float clampedAlpha = std::clamp(alpha, 0.f, 1.f);

    if (m_imageComponent)
    {
        auto col = m_hasBaseImageColor ? m_baseImageColor : m_imageComponent->color;
        col.w = col.w * clampedAlpha;
        m_imageComponent->color = col;
    }

    if (m_textComponent && m_hasBaseTextColor)
    {
        auto col = m_baseTextColor;
        col.w = col.w * clampedAlpha;
        m_textComponent->SetColor(col);
    }
}
