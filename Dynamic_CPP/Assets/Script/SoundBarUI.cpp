#include "SoundBarUI.h"
#include "pch.h"
#include "SoundManager.h"
#include "ImageComponent.h"
#include "RectTransformComponent.h"
#include "InputManager.h"

void SoundBarUI::Start()
{
    m_soundBarRect = GetComponent<RectTransformComponent>();
    m_soundBarImageComponent = GetComponent<ImageComponent>();
    if (!m_soundBarRect || !m_soundBarImageComponent) return;

    if (!GetOwner()->m_childrenIndices.empty())
    {
        const int childObject = GetOwner()->m_childrenIndices[0];
        if (auto childGameObject = GameObject::FindIndex(childObject))
        {
            m_soundBarButtonRect = childGameObject->GetComponent<RectTransformComponent>();
            m_soundBarButtonImageComponent = childGameObject->GetComponent<ImageComponent>();
        }
    }
    if (!m_soundBarButtonRect || !m_soundBarButtonImageComponent) return;

    RecalculateBarRange();

    // 초기값: 100% 위치에 버튼 놓기 (원하면 0으로 바꿔도 됨)
    SetVolumePercent(100);
}

void SoundBarUI::Update(float tick)
{
    if (m_soundBarImageComponent)
    {
        if(m_soundBarImageComponent->IsNavigationThis())
        {
            m_soundBarImageComponent->color = Mathf::Color4(1.f, 1.f, 1.f, 1);
            m_soundBarButtonImageComponent->color = Mathf::Color4(1, 1, 1, 1);

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

            float x = axis.x;

            const float ax = std::fabs(x);
            if (ax < m_deadzone) return;

			const float sign = (x >= 0.f) ? 1.f : -1.f;
			const float norm = (ax - m_deadzone) / (1.f - m_deadzone); // 데드존 이후 구간 정규화
            const float scaled = sign * norm;

			float delta = scaled * m_unitsPerSec * 1.5f * tick; // 초당 변화량 적용
			int   p = m_percent;
            m_percent = static_cast<int>(std::round(static_cast<float>(p) + delta));

			// 클램프
			m_percent = std::clamp(m_percent, 0, 100);

            // 버튼 X 배치
            auto btnPos = m_soundBarButtonRect->GetAnchoredPosition();
            btnPos.x = CalcButtonXFromPercent(m_percent);
			m_soundBarButtonRect->SetAnchoredPosition(btnPos);

			Sound->setMasterVolume(static_cast<float>(m_percent) / 100.f);
        }
        else
        {
			m_soundBarImageComponent->color = Mathf::Color4(0.7f, 0.7f, 0.7f, 1);
			m_soundBarButtonImageComponent->color = Mathf::Color4(0.7f, 0.7f, 0.7f, 1);
        }
    }
}

void SoundBarUI::SetVolumePercent(int percent)
{
    m_percent = std::clamp(percent, 0, 100);

    // 버튼 X 배치
    auto btnPos = m_soundBarButtonRect->GetAnchoredPosition();
    btnPos.x = CalcButtonXFromPercent(m_percent);
    m_soundBarButtonRect->SetAnchoredPosition(btnPos);
}

void SoundBarUI::RecalculateBarRange()
{
    // 사운드 바 기준 정보
    const auto barPos = m_soundBarRect->GetAnchoredPosition();
    const auto barSize = m_soundBarRect->GetSizeDelta();

    // 버튼 기준 정보
    const auto btnSize = m_soundBarButtonRect->GetSizeDelta();

    m_barCenterX = barPos.x;
    m_barHalfW = barSize.x * 0.5f;
    m_btnHalfW = btnSize.x * 0.5f;

    // “렉트 중앙 위치 ± 이미지 사이즈 절반”을 바의 0~100%로 사용.
    // 버튼이 넘어가지 않도록 버튼 절반 폭을 제외한 범위를 실제 이동 한계로 잡음.
    const float logicalMin = m_barCenterX - m_barHalfW; // 0% 지점(논리)
    const float logicalMax = m_barCenterX + m_barHalfW; // 100% 지점(논리)

    // 실제 버튼 이동 한계(클램프용)
    m_minX = logicalMin + m_btnHalfW;
    m_maxX = logicalMax - m_btnHalfW;

    // 1% 스텝 길이 (버튼이 실제로 움직일 수 있는 거리 기준)
    const float travel = m_maxX - m_minX;
    m_stepX = (travel > 0.f) ? (travel / 100.f) : 0.f;
}

float SoundBarUI::CalcButtonXFromPercent(int percent) const
{
    const int  p = std::clamp(percent, 0, 100);
    return m_minX + m_stepX * static_cast<float>(p);
}

int SoundBarUI::CalcPercentFromButtonX(float x) const
{
    if (m_stepX <= 0.f) return 0;
    const float t = (std::clamp(x, m_minX, m_maxX) - m_minX) / m_stepX;
    // 1% 단위 스냅
    int p = static_cast<int>(std::round(t));
    return std::clamp(p, 0, 100);
}

