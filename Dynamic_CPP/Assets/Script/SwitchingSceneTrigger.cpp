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
    for (int index = 1;; ++index)
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

void SwitchingSceneTrigger::SetupCutRangeForNextScene()
{
    // 한 번만 세팅
    if (m_cutRangeReady) return;

    int nextSceneType = m_gameManager ? m_gameManager->m_nextSceneIndex : -1;

    int start = 0;
    int end = 0;

    // 보스 or 테스트 모드 → 3~7 (end=8)
    if (nextSceneType == (int)SceneType::Boss || m_isTestBossStage)
    {
        start = 3;
        end = 8;
    }
    else // 일반 스테이지 → 0~2 (end=3)
    {
        start = 0;
        end = 3;
    }

    // 이미지 개수에 맞춰 클램프
    const int N = static_cast<int>(m_cutImages.size());
    start = std::clamp(start, 0, N);
    end = std::clamp(end, start, N); // end >= start && end <= N

    m_cutStart = start;
    m_cutEndExclusive = end;
    m_cutCursor = m_cutStart;
    m_cutRangeReady = true;

    // 자동 진행 타이머 초기화
    m_autoTimer = 0.f;
}

inline bool HasCutsceneForNextSceneStrict(const GameManager* gm)
{
    if (!gm) return false;
    int t = gm->m_nextSceneIndex; int prev = gm->m_prevSceneIndex;
    return prev == (int)SceneType::SelectChar || t == (int)SceneType::Boss;
}

void SwitchingSceneTrigger::ShowCut(int idx)
{
    if (idx >= 0 && idx < static_cast<int>(m_cutImages.size()) && m_cutImages[idx])
        m_cutImages[idx]->SetEnabled(true);
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
        float t = std::clamp(m_timer / std::max(0.0001f, m_fadeInDuration), 0.f, 1.f);
        SetAlphaAll(SmoothStep(t));

        if (t >= 1.f) {
            m_timer = 0.f;

            if (!HasCutsceneForNextSceneStrict(m_gameManager)) {
                m_phase = SwitchPhase::WaitingInput;
            }
            else {
                m_phase = SwitchPhase::WaitingInput;
                SetupCutRangeForNextScene(); // 여기는 OK
            }
        }
        break;
    }

    case SwitchPhase::WaitingInput:
    {
        // 수동 입력: 즉시 다음 컷
        if (IsAnyAJustPressed())
        {
            int nextSceneType = m_gameManager ? m_gameManager->m_nextSceneIndex : -1;
            const bool hasCutscene = HasCutsceneForNextSceneStrict(m_gameManager);

            if (!hasCutscene)
            {
                // 컷신 없음: 자동으로 바로 페이드아웃
                m_phase = SwitchPhase::FadingOut;
                m_timer = 0.f;
                break;
            }

            if (m_cutCursor < m_cutEndExclusive)
            {
                ShowCut(m_cutCursor);
                ++m_cutCursor;
                m_autoTimer = 0.f; // 수동 시 타이머 리셋
            }
            else
            {
                // 이미 마지막 컷까지 끝난 상태 → 페이드아웃
                m_phase = SwitchPhase::FadingOut;
                m_timer = 0.f;
                break;
            }
        }
        else // 자동 재생
        {
            int nextSceneType = m_gameManager ? m_gameManager->m_nextSceneIndex : -1;
            const bool hasCutscene = HasCutsceneForNextSceneStrict(m_gameManager);

            if (hasCutscene)
            {
                if (m_cutCursor < m_cutEndExclusive)
                {
                    m_autoTimer += tick;
                    if (m_autoTimer >= m_autoPlayDelay)
                    {
                        m_autoTimer = 0.f;
                        ShowCut(m_cutCursor);
                        ++m_cutCursor;
                    }
                }
                else
                {
                    // 마지막 컷 도달
                    if (!m_waitAtLastCut)
                    {
                        // 자동으로 바로 넘어가고 싶다면:
                        m_phase = SwitchPhase::FadingOut;
                        m_timer = 0.f;
                    }
                    // else: 입력을 기다림 (아무 것도 하지 않음)
                }
            }
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

