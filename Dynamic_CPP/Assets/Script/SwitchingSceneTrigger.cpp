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

	GameObject* loadingObj = GameObject::Find("LoadingText");
    if (loadingObj)
    {
        m_loadingText = loadingObj->GetComponent<TextComponent>();
	}

    m_cutImages.clear();
    m_cutImages.reserve(32);

    const std::string prefix = "Cut";
    for (int index = 0;; ++index)
    {
        const std::string name = prefix + std::to_string(index);
        GameObject* obj = GameObject::Find(name);
        if (!obj)
            break; // 첫 빈 슬롯에서 종료 (CutN이 없으면 루프 끝)

        if (ImageComponent* comp = obj->GetComponent<ImageComponent>())
        {
            m_cutImages.push_back(comp);
        }
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

void SwitchingSceneTrigger::SetAlphaAll(float a)
{
    if (m_buttonText)    m_buttonText->SetAlpha(a);
    if (m_switchingText) m_switchingText->SetAlpha(a);
}

void SwitchingSceneTrigger::Update(float tick)
{
    if (!m_buttonText || !m_switchingText) return;

    const bool ready = GameInstance::GetInstance()->IsLoadSceneComplete();

    // 로딩 미완료 시 항상 숨김 상태로 초기화
    if (!ready) {
        m_phase = SwitchPhase::Hidden;
        m_timer = 0.f;
        SetAlphaAll(0.f);
        // 버튼 에지 상태도 초기화(선택 사항)
        m_prevA0 = m_prevA1 = false;
        return; // 다른 로직이 필요하면 여기서 continue 패턴으로 변경
    }

    switch (m_phase)
    {
    case SwitchPhase::Hidden: {
        // 로딩 완료되면 등장 시작
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
        // 연출: 버튼 텍스트에만 살짝 펄스 주고 싶다면(선택)
        // float pulse = 0.85f + 0.15f * std::sin(totalTime * 6.283f * 1.0f);
        // m_buttonText->SetAlpha(pulse);

        if (IsAnyAJustPressed()) 
        {
            int nextSceneType = m_gameManager->m_nextSceneIndex;
            if (nextSceneType == (int)SceneType::Tutorial)
            {
                m_phase = SwitchPhase::FadingOut;
                m_timer = 0.f;
            }
            else
            {
                if (0 == m_cutsceneIndex)
                {
                    constexpr int BOSS_CUT_SCENE_INDEX = 3;
                    if(nextSceneType == (int)SceneType::Boss)
                    {
                        m_cutsceneIndex = BOSS_CUT_SCENE_INDEX;
                        m_maxCutsceneIndex = 8;
                    }
                    else
                    {
                        m_maxCutsceneIndex = 3;
                    }
                }

                if (m_maxCutsceneIndex > m_cutsceneIndex)
                {
                    if (m_cutImages[m_cutsceneIndex]);
                    {
                        m_cutImages[m_cutsceneIndex++]->SetEnabled(true);
                    }
                }
                else
                {
                    m_phase = SwitchPhase::FadingOut;
                    m_timer = 0.f;
                }
            }
            // 즉시 깜빡임 방지로 현재 알파를 고정해도 됨
        }
        break;
    }

    case SwitchPhase::FadingOut: {
        m_timer += tick;
        const float t = std::clamp(m_timer / std::max(0.0001f, m_fadeOutDuration), 0.f, 1.f);
        const float a = 1.f - SmoothStep(t); // 점점 어둡게
        SetAlphaAll(a);
        if (m_loadingText)
        {
            m_loadingText->SetAlpha(a);
        }

        if (t >= 1.f) {
            m_phase = SwitchPhase::Switching;
        }
        break;
    }

    case SwitchPhase::Switching: {
        // 한 번만 호출되도록 상태로 보호
        if (m_gameManager) {
            m_gameManager->SwitchNextScene();
        }
        // 이후 이 객체의 라이프사이클에 따라 Hidden으로 돌리거나 그대로 둠
        m_phase = SwitchPhase::Hidden;
        m_timer = 0.f;
        SetAlphaAll(0.f);
        if (m_loadingText)
        {
            m_loadingText->SetAlpha(0.f);
        }
        break;
    }
    }
}

