#include "LetterboxController.h"
#include "RectTransformComponent.h"
#include "ImageComponent.h"
#include "Camera.h"
#include "SceneManager.h"
#include "PlayerDialogueUI.h"
#include "DialogueConductor.h"
#include "EntityAsis.h"
#include "Player.h"


void LetterboxController::Start()
{
    // 바 검색
    m_top = FindBar(topBarName);
    m_bottom = FindBar(bottomBarName);

    auto screen = GetScreenSize();

    // 초기 세팅
    SetupBar(m_top,    /*isTop=*/true, screen);
    SetupBar(m_bottom, /*isTop=*/false, screen);

    // 시작 배치
    m_isCinema = startInCinema;
    PlaceBarsImmediate(/*inView=*/m_isCinema, screen);

    // 애니메이션 초기화
    m_isAnimating = false;
    m_elapsed = 0.0f;

    m_dialogueConductor = GetOwner()->GetComponent<DialogueConductor>();

    m_p1BubbleObj = GameObject::Find("P1Dialog");
    m_p2BubbleObj = GameObject::Find("P2Dialog");

    if (m_dialogueConductor)
    {
        if (m_p1BubbleObj)
        {
            m_p1Bubble = m_p1BubbleObj->GetComponent<PlayerDialogueUI>();
        }
        if (m_p2BubbleObj)
        {
            m_p2Bubble = m_p2BubbleObj->GetComponent<PlayerDialogueUI>();
        }
		m_dialogueConductor->BindBubbles(m_p1Bubble, m_p2Bubble);
	}

	auto p1 = GameObject::Find("1P");
    auto p2 = GameObject::Find("2P");
    if (m_dialogueConductor)
    {
        m_dialogueConductor->SetParticipants(p1->shared_from_this(), p2->shared_from_this());
        m_dialogueConductor->BindLetterbox(this);
	}

    SetBubbleVisible(m_isCinema);
}

void LetterboxController::Update(float tick)
{
    if (!m_top.rect || !m_bottom.rect)
        return;

    if (!m_isAnimating)
        return;

    m_elapsed += tick;
    float t = std::clamp(m_elapsed / std::max(0.0001f, animDuration), 0.0f, 1.0f);
    // SmoothStep 이징
    t = t * t * (3.0f - 2.0f * t);

    auto screen = GetScreenSize();

    // from/to 계산 (top)
    float topFromY = m_animFromInView ? (barHeight * 0.5f) : (-barHeight * 0.5f);
    float topToY = m_animToInView ? (barHeight * 0.5f) : (-barHeight * 0.5f);

    // from/to 계산 (bottom)
    float botFromY = m_animFromInView ? (screen.height - barHeight * 0.5f) : (screen.height + barHeight * 0.5f);
    float botToY = m_animToInView ? (screen.height - barHeight * 0.5f) : (screen.height + barHeight * 0.5f);

    float topY = std::lerp(topFromY, topToY, t);
    float botY = std::lerp(botFromY, botToY, t);

    m_top.rect->SetAnchoredPosition({ screen.width * 0.5f, topY });
    m_bottom.rect->SetAnchoredPosition({ screen.width * 0.5f, botY });

    if (m_elapsed >= animDuration)
    {
        m_isAnimating = false;
        m_isCinema = m_animToInView;
    }
}

void LetterboxController::EnterCinemaMode()
{
    StartAnim(/*toInView=*/true);
    auto asisObj = GameObject::Find("Asis");
    if (asisObj)
    {
        auto asis = asisObj->GetComponent<EntityAsis>();
        if (asis)
        {
            asis->SetMove(false);
        }
	}

	auto P1Controller = GameObject::Find("P1_UIController");
    if (P1Controller)
    {
		P1Controller->SetEnabled(false);
	}

    auto P2Controller = GameObject::Find("P2_UIController");
    if (P2Controller)
    {
        P2Controller->SetEnabled(false);
	}

	auto P1_HPBar = GameObject::Find("P1_HPBar");
    if (P1_HPBar)
    {
        P1_HPBar->SetEnabled(false);
	}

    auto P2_HPBar = GameObject::Find("P2_HPBar");
    if (P2_HPBar)
    {
        P2_HPBar->SetEnabled(false);
	}

    SetBubbleVisible(true);
}

void LetterboxController::ExitCinemaMode()
{
    StartAnim(/*toInView=*/false);
    auto asisObj = GameObject::Find("Asis");
    if (asisObj)
    {
        auto asis = asisObj->GetComponent<EntityAsis>();
        if (asis)
        {
            asis->SetMove(true);
        }
    }

	auto P1Controller = GameObject::Find("P1_UIController");
    if (P1Controller)
    {
        P1Controller->SetEnabled(true);
    }
    auto P2Controller = GameObject::Find("P2_UIController");
    if (P2Controller)
    {
        P2Controller->SetEnabled(true);
	}

    auto P1_HPBar = GameObject::Find("P1_HPBar");
    if (P1_HPBar)
    {
        P1_HPBar->SetEnabled(true);
    }

    auto P2_HPBar = GameObject::Find("P2_HPBar");
    if (P2_HPBar)
    {
        P2_HPBar->SetEnabled(true);
	}

	SetBubbleVisible(false);
}

void LetterboxController::TestCinemaMode()
{
    EnterCinemaMode();

    m_dialogueConductor->SetAlternatingRange(0, 6, DialogueConductor::Speaker::P1);
}

void LetterboxController::Stap1()
{
    EnterCinemaMode();

    m_dialogueConductor->SetAlternatingRange(0, 3, DialogueConductor::Speaker::P2);
}

void LetterboxController::Stap2()
{
    EnterCinemaMode();

    m_dialogueConductor->SetAlternatingRange(3, 7, DialogueConductor::Speaker::P1);
}

void LetterboxController::SetBubbleVisible(bool visible)
{
    // GameObject 레벨로 켜고/끄기 (업데이트도 함께 막고 싶으면 권장)
    //if (m_p1BubbleObj) m_p1BubbleObj->SetEnabled(visible);
    //if (m_p2BubbleObj) m_p2BubbleObj->SetEnabled(visible);

    // 안전하게 이미지도 끄기 (off 전환 시 잔상 방지)
    if (!visible)
    {
        if (m_p1Bubble) m_p1Bubble->Hide();
        if (m_p2Bubble) m_p2Bubble->Hide();
    }
}

// === 내부 구현 ===

LetterboxController::Size LetterboxController::GetScreenSize() const
{
    if (auto cam = CameraManagement->GetLastCamera())
        return { cam->GetScreenSize().width, cam->GetScreenSize().height };
    return { 1920.0f, 1080.0f };
}

LetterboxController::Bar LetterboxController::FindBar(const std::string& name)
{
    Bar b{};
    if (auto go = GameObject::Find(name))
    {
        b.obj = go;
        b.rect = go->GetComponent<RectTransformComponent>();
        b.img = go->GetComponent<ImageComponent>();
    }
    return b;
}

void LetterboxController::SetupBar(Bar& b, bool /*isTop*/, const Size& screen)
{
    if (!b.rect || !b.img) return;

    // 가로 전체 폭, 세로 barHeight
    //b.rect->SetSizeDelta({ screen.width, barHeight });

    // 검은색 고정(텍스처 없이 컬러만으로 가능)
    //b.img->color = { 0.f, 0.f, 0.f, 1.f };
}

void LetterboxController::PlaceBarsImmediate(bool inView, const Size& screen)
{
    if (m_top.rect) {
        float y = inView ? (barHeight * 0.5f) : (-barHeight * 0.5f);
        m_top.rect->SetAnchoredPosition({ screen.width * 0.5f, y });
    }
    if (m_bottom.rect) {
        float y = inView ? (screen.height - barHeight * 0.5f) : (screen.height + barHeight * 0.5f);
        m_bottom.rect->SetAnchoredPosition({ screen.width * 0.5f, y });
    }
}

void LetterboxController::StartAnim(bool toInView)
{
    // 현재 상태에서 목표 상태로
    m_animFromInView = m_isCinema;
    m_animToInView = toInView;
    m_isAnimating = true;
    m_elapsed = 0.0f;
}
