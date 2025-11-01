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
            break; // ù �� ���Կ��� ���� (CutN�� ������ ���� ��)

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
    // �� ���� ����
    if (m_cutRangeReady) return;

    int nextSceneType = m_gameManager ? m_gameManager->m_nextSceneIndex : -1;

    int start = 0;
    int end = 0;

    // ���� or �׽�Ʈ ��� �� 3~7 (end=8)
    if (nextSceneType == (int)SceneType::Boss || m_isTestBossStage)
    {
        start = 3;
        end = 8;
    }
    else // �Ϲ� �������� �� 0~2 (end=3)
    {
        start = 0;
        end = 3;
    }

    // �̹��� ������ ���� Ŭ����
    const int N = static_cast<int>(m_cutImages.size());
    start = std::clamp(start, 0, N);
    end = std::clamp(end, start, N); // end >= start && end <= N

    m_cutStart = start;
    m_cutEndExclusive = end;
    m_cutCursor = m_cutStart;
    m_cutRangeReady = true;

    // �ڵ� ���� Ÿ�̸� �ʱ�ȭ
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
        float t = std::clamp(m_timer / std::max(0.0001f, m_fadeInDuration), 0.f, 1.f);
        SetAlphaAll(SmoothStep(t));

        if (t >= 1.f) {
            m_timer = 0.f;

            if (!HasCutsceneForNextSceneStrict(m_gameManager)) {
                m_phase = SwitchPhase::WaitingInput;
            }
            else {
                m_phase = SwitchPhase::WaitingInput;
                SetupCutRangeForNextScene(); // ����� OK
            }
        }
        break;
    }

    case SwitchPhase::WaitingInput:
    {
        // ���� �Է�: ��� ���� ��
        if (IsAnyAJustPressed())
        {
            int nextSceneType = m_gameManager ? m_gameManager->m_nextSceneIndex : -1;
            const bool hasCutscene = HasCutsceneForNextSceneStrict(m_gameManager);

            if (!hasCutscene)
            {
                // �ƽ� ����: �ڵ����� �ٷ� ���̵�ƿ�
                m_phase = SwitchPhase::FadingOut;
                m_timer = 0.f;
                break;
            }

            if (m_cutCursor < m_cutEndExclusive)
            {
                ShowCut(m_cutCursor);
                ++m_cutCursor;
                m_autoTimer = 0.f; // ���� �� Ÿ�̸� ����
            }
            else
            {
                // �̹� ������ �Ʊ��� ���� ���� �� ���̵�ƿ�
                m_phase = SwitchPhase::FadingOut;
                m_timer = 0.f;
                break;
            }
        }
        else // �ڵ� ���
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
                    // ������ �� ����
                    if (!m_waitAtLastCut)
                    {
                        // �ڵ����� �ٷ� �Ѿ�� �ʹٸ�:
                        m_phase = SwitchPhase::FadingOut;
                        m_timer = 0.f;
                    }
                    // else: �Է��� ��ٸ� (�ƹ� �͵� ���� ����)
                }
            }
        }
        break;
    }

    case SwitchPhase::FadingOut: {
        m_timer += tick;
        const float t = std::clamp(m_timer / std::max(0.0001f, m_fadeOutDuration), 0.f, 1.f);
        const float a = 1.f - SmoothStep(t); // ���� ��Ӱ�
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
        // �� ���� ȣ��ǵ��� ���·� ��ȣ
        if (m_gameManager) {
            m_gameManager->SwitchNextScene();
        }
        // ���� �� ��ü�� ����������Ŭ�� ���� Hidden���� �����ų� �״�� ��
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

