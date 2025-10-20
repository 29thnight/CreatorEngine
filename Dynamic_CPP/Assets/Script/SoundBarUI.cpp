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

    // �ʱⰪ: 100% ��ġ�� ��ư ���� (���ϸ� 0���� �ٲ㵵 ��)
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
			const float norm = (ax - m_deadzone) / (1.f - m_deadzone); // ������ ���� ���� ����ȭ
            const float scaled = sign * norm;

			float delta = scaled * m_unitsPerSec * 1.5f * tick; // �ʴ� ��ȭ�� ����
			int   p = m_percent;
            m_percent = static_cast<int>(std::round(static_cast<float>(p) + delta));

			// Ŭ����
			m_percent = std::clamp(m_percent, 0, 100);

            // ��ư X ��ġ
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

    // ��ư X ��ġ
    auto btnPos = m_soundBarButtonRect->GetAnchoredPosition();
    btnPos.x = CalcButtonXFromPercent(m_percent);
    m_soundBarButtonRect->SetAnchoredPosition(btnPos);
}

void SoundBarUI::RecalculateBarRange()
{
    // ���� �� ���� ����
    const auto barPos = m_soundBarRect->GetAnchoredPosition();
    const auto barSize = m_soundBarRect->GetSizeDelta();

    // ��ư ���� ����
    const auto btnSize = m_soundBarButtonRect->GetSizeDelta();

    m_barCenterX = barPos.x;
    m_barHalfW = barSize.x * 0.5f;
    m_btnHalfW = btnSize.x * 0.5f;

    // ����Ʈ �߾� ��ġ �� �̹��� ������ ���ݡ��� ���� 0~100%�� ���.
    // ��ư�� �Ѿ�� �ʵ��� ��ư ���� ���� ������ ������ ���� �̵� �Ѱ�� ����.
    const float logicalMin = m_barCenterX - m_barHalfW; // 0% ����(��)
    const float logicalMax = m_barCenterX + m_barHalfW; // 100% ����(��)

    // ���� ��ư �̵� �Ѱ�(Ŭ������)
    m_minX = logicalMin + m_btnHalfW;
    m_maxX = logicalMax - m_btnHalfW;

    // 1% ���� ���� (��ư�� ������ ������ �� �ִ� �Ÿ� ����)
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
    // 1% ���� ����
    int p = static_cast<int>(std::round(t));
    return std::clamp(p, 0, 100);
}

