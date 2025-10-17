#include "SwitchingSceneTrigger.h"
#include "TextComponent.h"
#include "ImageComponent.h"
#include "InputManager.h"
#include "GameInstance.h"
#include "GameManager.h"
#include "pch.h"

static float SmoothStep(float x) 
{ // [0,1] -> [0,1]
	x = std::clamp(x, 0.0f, 1.0f);
	return x * x * (3.f - 2.f * x);
}

void SwitchingSceneTrigger::Start()
{
	m_buttonText = GetOwner()->GetComponent<TextComponent>();
	if (m_buttonText)
	{
		m_buttonText->SetAlpha(0);
	}
	int childIndex = GetOwner()->m_childrenIndices[0];
	GameObject* child = GameObject::FindIndex(childIndex);
	if (child)
	{
		m_switchingText = child->GetComponent<TextComponent>();
		if (m_switchingText)
		{
			m_switchingText->SetAlpha(0);
		}
	}

	GameObject* gmObj = GameObject::Find("GameManager");
	if (gmObj)
	{
		m_gameManager = gmObj->GetComponent<GameManager>();
	}
}

bool SwitchingSceneTrigger::IsAnyAJustPressed()
{
	const bool a0 = InputManagement->IsControllerButtonDown(0, ControllerButton::A);
	const bool a1 = InputManagement->IsControllerButtonDown(1, ControllerButton::A);

	const bool just0 = (a0 && !m_prevA0);
	const bool just1 = (a1 && !m_prevA1);

	m_prevA0 = a0;
	m_prevA1 = a1;
	return just0 || just1;
}

void SwitchingSceneTrigger::Update(float tick)
{
    if (!m_buttonText || !m_switchingText) return;

    const bool ready = GameInstance::GetInstance()->IsLoadSceneComplete();

    // �ε� �̿Ϸ� �� �׻� ���� ���·� �ʱ�ȭ
    if (!ready) {
        m_phase = SwitchPhase::Hidden;
        m_timer = 0.f;
        SetAlphaAll(0.f);
        // ��ư ���� ���µ� �ʱ�ȭ(���� ����)
        m_prevA0 = m_prevA1 = false;
        return; // �ٸ� ������ �ʿ��ϸ� ���⼭ continue �������� ����
    }

    switch (m_phase)
    {
    case SwitchPhase::Hidden: {
        // �ε� �Ϸ�Ǹ� ���� ����
        m_timer = 0.f;
        m_phase = SwitchPhase::FadingIn;
        SetAlphaAll(0.f);
        break;
    }

    case SwitchPhase::FadingIn: {
        m_timer += tick;
        const float t = std::clamp(m_timer / std::max(0.0001f, m_fadeInDuration), 0.f, 1.f);
        const float a = SmoothStep(t);
        SetAlphaAll(a);

        if (t >= 1.f) {
            m_phase = SwitchPhase::WaitingInput;
            m_timer = 0.f;
        }
        break;
    }

    case SwitchPhase::WaitingInput: {
        // ����: ��ư �ؽ�Ʈ���� ��¦ �޽� �ְ� �ʹٸ�(����)
        // float pulse = 0.85f + 0.15f * std::sin(totalTime * 6.283f * 1.0f);
        // m_buttonText->SetAlpha(pulse);

        if (IsAnyAJustPressed()) {
            m_phase = SwitchPhase::FadingOut;
            m_timer = 0.f;
            // ��� ������ ������ ���� ���ĸ� �����ص� ��
        }
        break;
    }

    case SwitchPhase::FadingOut: {
        m_timer += tick;
        const float t = std::clamp(m_timer / std::max(0.0001f, m_fadeOutDuration), 0.f, 1.f);
        const float a = 1.f - SmoothStep(t); // ���� ��Ӱ�
        SetAlphaAll(a);

        if (t >= 1.f) {
            m_phase = SwitchPhase::Switching;
        }
        break;
    }

    case SwitchPhase::Switching: {
        // �� ���� ȣ��ǵ��� ���·� ��ȣ
        if (m_gameManager) {
            m_gameManager->SwitchNextScene();
        }
        // ���� �� ��ü�� ����������Ŭ�� ���� Hidden���� �����ų� �״�� ��
        m_phase = SwitchPhase::Hidden;
        m_timer = 0.f;
        SetAlphaAll(0.f);
        break;
    }
    }
}

