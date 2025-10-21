#include "ViveSwitchUI.h"
#include "ImageComponent.h"
#include "RectTransformComponent.h"
#include "InputManager.h"
#include "GameInstance.h"
#include "pch.h"

void ViveSwitchUI::Start()
{
    m_barRect = GetComponent<RectTransformComponent>();
    m_barImage = GetComponent<ImageComponent>();
    if (!m_barRect || !m_barImage) return;

    if (!GetOwner()->m_childrenIndices.empty())
    {
        const int child = GetOwner()->m_childrenIndices[0];
        if (auto childGo = GameObject::FindIndex(child))
        {
            m_btnRect = childGo->GetComponent<RectTransformComponent>();
            m_btnImage = childGo->GetComponent<ImageComponent>();
        }
    }
    if (!m_btnRect || !m_btnImage) return;

    RecalculatePositions();

    // 초기 상태(원하면 false로 시작)
    SetViveEnabled(true);
}

void ViveSwitchUI::Update(float tick)
{
	m_haptics.Update(tick);

    if (!m_barImage) return;

    if (m_barImage->IsNavigationThis())
    {
		m_barImage->color = Mathf::Color4(1, 1, 1, 1);
		m_btnImage->color = Mathf::Color4(1, 1, 1, 1);

        if (m_cdTimer > 0.f) m_cdTimer -= tick;

        auto axis0 = InputManagement->GetControllerThumbR(0);
        auto axis1 = InputManagement->GetControllerThumbR(1);
        Mathf::Vector2 axis{};

        if (axis0 == Mathf::Vector2::Zero)
        {
            axis = axis1;
        }
        else
        {
            axis = axis0;
        }

        if (axis == Mathf::Vector2::Zero) return;

        const float x = axis.x;

        if (m_cdTimer <= 0.f)
        {
            if (x <= -m_toggleThreshold && !IsViveEnabled())
            {
                SetViveEnabled(true);
                m_cdTimer = m_cooldownSec;
            }
            else if (x >= m_toggleThreshold && IsViveEnabled())
            {
                SetViveEnabled(false);
                m_cdTimer = m_cooldownSec;
            }
        }

        if (!m_useSmoothMove || !m_btnRect) return;

        const float targetX = m_isViveEnabled ? m_offX : m_onX;
        auto pos = m_btnRect->GetAnchoredPosition();

        // 선형 보간(부드러운 슬라이드)
        const float step = m_moveSpeed * tick; // 초당 m_moveSpeed px
        if (std::fabs(pos.x - targetX) <= step)
            pos.x = targetX;
        else
            pos.x += (pos.x < targetX ? step : -step);

        m_btnRect->SetAnchoredPosition(pos);
    }
    else
    {
		m_barImage->color = Mathf::Color4(0.7f, 0.7f, 0.7f, 1);
		m_btnImage->color = Mathf::Color4(0.7f, 0.7f, 0.7f, 1);
    }
}

void ViveSwitchUI::SetViveEnabled(bool enable)
{
	if (m_isViveEnabled == enable) return;

    m_isViveEnabled = enable;
	GameInstance::GetInstance()->SetViveEnabled(m_isViveEnabled);
	ApplyButton();
}

void ViveSwitchUI::ToggleViveEnabled()
{
    SetViveEnabled(!m_isViveEnabled);
}

void ViveSwitchUI::RecalculatePositions()
{
    const auto barPos = m_barRect->GetAnchoredPosition();
    const auto barSize = m_barRect->GetSizeDelta();
    const auto btnSize = m_btnRect->GetSizeDelta();

    m_centerX = barPos.x;
    m_barHalfW = barSize.x * 0.5f;
    m_btnHalfW = btnSize.x * 0.5f;

    // 중앙 기준 ± 바 폭의 1/4
    const float rawOff = m_centerX - (barSize.x * 0.25f);
    const float rawOn = m_centerX + (barSize.x * 0.25f);

    // 버튼이 삐져나오지 않도록 버튼 절반폭을 제외한 이동 한계로 클램프
    const float minX = (m_centerX - m_barHalfW) + m_btnHalfW;
    const float maxX = (m_centerX + m_barHalfW) - m_btnHalfW;

    m_offX = std::clamp(rawOff, minX, maxX);
    m_onX = std::clamp(rawOn, minX, maxX);
}

void ViveSwitchUI::ApplyButton()
{
    if (!m_btnRect) return;

    auto pos = m_btnRect->GetAnchoredPosition();
    const float targetX = m_isViveEnabled ? m_offX : m_onX;

    if (m_isViveEnabled && !m_prevViveEnabled)
    {
		if (InputManagement->IsControllerConnected(0))
        {
            m_haptics.PlayHeartbeatStrong(0, /*ampMul=*/1.2f, /*repeat=*/2);
        }

        if (InputManagement->IsControllerConnected(1))
        {
            m_haptics.PlayHeartbeatStrong(1, /*ampMul=*/1.2f, /*repeat=*/2);
		}
    }
    m_prevViveEnabled = m_isViveEnabled;

    if (!m_useSmoothMove)
    {
        pos.x = targetX;
        m_btnRect->SetAnchoredPosition(pos);
        return;
    }

    // (선택) 부드러운 보간
    // tick은 Update에서 처리
    pos.x = targetX; // 초기 적용은 스냅
    m_btnRect->SetAnchoredPosition(pos);
}

