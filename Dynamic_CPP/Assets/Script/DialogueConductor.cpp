#include "DialogueConductor.h"
#include "PlayerDialogueUI.h"
#include "InputManager.h"
#include "LetterboxController.h"
#include "Player.h"
#include "LogSystem.h"
#include "pch.h"

void DialogueConductor::MaybeStartOrTickAutoExit(float tick)
{
    const bool finished = (m_cursor >= static_cast<int>(m_sequence.size()));
    if (!finished) return;

    // 이미 종료 대기 중이면 타이머만 갱신
    if (m_exiting)
    {
        m_exitTimer += tick;
        if (m_exitTimer >= autoExitDelay)
        {
            m_exiting = false;
            m_exitTimer = 0.f;
            if (m_letterbox && autoExitAfterFinish)
            {
                m_letterbox->ExitCinemaMode();
            }
        }
        return;
    }

    // 아직 대기 시작 안 했고, 자동 종료를 써야 하면 스타트
    if (autoExitAfterFinish)
    {
        m_exiting = true;
        m_exitTimer = 0.f;

        // 마지막 컷 직후 버블은 숨길지 여부
        if (!waitAtLast)
        {
            if (m_p1Bubble) m_p1Bubble->Hide();
            if (m_p2Bubble) m_p2Bubble->Hide();
        }
    }
}

void DialogueConductor::BindBubbles(PlayerDialogueUI* p1Bubble, PlayerDialogueUI* p2Bubble)
{
    m_p1Bubble = p1Bubble;
    m_p2Bubble = p2Bubble;
}

void DialogueConductor::SetParticipants(std::shared_ptr<GameObject> p1, std::shared_ptr<GameObject> p2)
{
    m_p1 = p1;
    m_p2 = p2;

    // 각 버블에 대상/비교대상 세팅 (피벗 자동 전환용)
    if (m_p1Bubble)
    {
        m_p1Bubble->SetTarget(p1);
        m_p1Bubble->SetCompareTarget(p2);
        m_p1Bubble->SetPlayerIndex(0);
    }
    if (m_p2Bubble)
    {
        m_p2Bubble->SetTarget(p2);
        m_p2Bubble->SetCompareTarget(p1);
        m_p2Bubble->SetPlayerIndex(1);
    }
}

void DialogueConductor::SetAlternatingRange(int startInclusive, int endExclusive, Speaker firstSpeaker)
{
    const int s = std::max(0, startInclusive);
    const int e = std::max(s, endExclusive);

    m_sequence.clear();
    m_sequence.reserve(e - s);

    Speaker cur = firstSpeaker;
    for (int idx = s; idx < e; ++idx)
    {
        m_sequence.push_back(Step{ cur, idx });
        cur = (cur == Speaker::P1) ? Speaker::P2 : Speaker::P1; // 번갈이
    }

    ResetPlayback();
}

void DialogueConductor::ClearSequence()
{
    m_sequence.clear();
    m_cursor = 0;
    m_autoTimer = 0.f;

    // ★ 자동 종료 타이머 리셋
    m_exiting = false;
    m_exitTimer = 0.f;

    if (hideWhenReset)
    {
        if (m_p1Bubble) m_p1Bubble->Hide();
        if (m_p2Bubble) m_p2Bubble->Hide();
    }
}

void DialogueConductor::AddStep(Speaker who, int textureId)
{
    m_sequence.push_back(Step{ who, textureId });
}

void DialogueConductor::ResetPlayback()
{
    m_cursor = 0;
    m_autoTimer = 0.f;

    // ★ 자동 종료 타이머 리셋
    m_exiting = false;
    m_exitTimer = 0.f;

    if (hideWhenReset)
    {
        if (m_p1Bubble) m_p1Bubble->Hide();
        if (m_p2Bubble) m_p2Bubble->Hide();
    }
}

bool DialogueConductor::IsAnyAJustPressed()
{
    const bool a0 = InputManagement->IsControllerButtonDown(0, ControllerButton::A);
    const bool a1 = InputManagement->IsControllerButtonDown(1, ControllerButton::A);

    const bool just0 = (a0 && !m_prevA0);
    const bool just1 = (a1 && !m_prevA1);

    m_prevA0 = a0;
    m_prevA1 = a1;
    return just0 || just1;
}

void DialogueConductor::StepShow()
{
    if (m_cursor >= static_cast<int>(m_sequence.size())) return;

    const Step& step = m_sequence[m_cursor];

    if (step.who == Speaker::P1)
    {
        if (m_p1Bubble) m_p1Bubble->ShowTexture(step.textureId);
        // 필요하면 반대쪽을 숨김 처리:
        // if (m_p2Bubble) m_p2Bubble->Hide();
    }
    else
    {
        if (m_p2Bubble) m_p2Bubble->ShowTexture(step.textureId);
        // if (m_p1Bubble) m_p1Bubble->Hide();
    }

    ++m_cursor;
}

void DialogueConductor::Update(float tick)
{
	static float elapsed = 0.f;
	constexpr float debugLogInterval = 2.f; // 5초마다 로그
	if (m_cursor >= static_cast<int>(m_sequence.size()))
    {
        elapsed += tick;
        if (elapsed >= debugLogInterval)
        {
            auto p1 = GameObject::Find("1P");
            if (p1)
            {
                auto player1 = p1->GetComponent<Player>();
                if (player1)
                {
                    player1->StagingEnd();
                }
            }
            auto p2 = GameObject::Find("2P");
            if (p2)
            {
                auto player2 = p2->GetComponent<Player>();
                if (player2)
                {
                    player2->StagingEnd();
                }
            }
            elapsed = 0.f;
		}
    }
    

    if (IsAnyAJustPressed())
    {
        if (m_cursor < static_cast<int>(m_sequence.size()))
        {
            StepShow();
            m_autoTimer = 0.f;
        }
        // else: 끝난 상태 아래에서 자동 종료 타이머 처리
    }
    else
    {
        if (m_cursor < static_cast<int>(m_sequence.size()))
        {
            m_autoTimer += tick;
            if (m_autoTimer >= autoPlayDelay)
            {
                m_autoTimer = 0.f;
                StepShow();
            }
        }
        // else: 끝난 상태 아래에서 자동 종료 타이머 처리
    }

    // ★ 시퀀스 끝났으면 자동 종료 타이머를 돌림
    MaybeStartOrTickAutoExit(tick);
}
