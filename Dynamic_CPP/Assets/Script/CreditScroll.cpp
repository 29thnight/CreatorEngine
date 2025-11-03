#include "CreditScroll.h"
#include "RectTransformComponent.h"
#include "InputManager.h"
#include "SceneTransitionUI.h"
#include "GameManager.h"
#include "pch.h"

void CreditScroll::Start()
{
    // 자신의 RectTransform 캐시
    m_rect = GetOwner() ? GetOwner()->GetComponent<RectTransformComponent>() : nullptr;
    if (!m_rect) return;

   /* m_transitionUIObject = */
	auto sceneTransitionObj = GameObject::Find("SceneTransition");
    if (sceneTransitionObj)
    {
        m_transitionUIObject = sceneTransitionObj->GetComponent<SceneTransitionUI>();
	}

    auto gameMgrObj = GameObject::Find("GameManager");
    if (gameMgrObj)
    {
        m_gameManager = gameMgrObj->GetComponent<GameManager>();
    }

    // 현재 X는 유지하고 Y만 시작 지점으로 세팅
    auto pos = m_rect->GetAnchoredPosition();
    pos.y = m_startY;
    m_rect->SetAnchoredPosition(pos); // anchored 기반으로 표준화된 위치 세팅
}

void CreditScroll::Update(float tick)
{
    if (!m_rect) return;

    // 기본 속도에 Y 키 가속 적용
    float speed = m_scrollSpeed;
    if (InputManagement->IsControllerButtonPressed(0, ControllerButton::Y) ||
        InputManagement->IsControllerButtonPressed(1, ControllerButton::Y))
    {
        speed *= m_fastMultiplier;
    }

    // 아래 방향(감소)으로 스크롤
    auto pos = m_rect->GetAnchoredPosition();
    pos.y -= speed * tick;

    // 목표 지점에서 정지(클램프)
    bool reachedEnd = false;
    if (pos.y < m_endY)
    {
        pos.y = m_endY;
        reachedEnd = true;
    }

    m_rect->SetAnchoredPosition(pos);

    // 4) 스크롤 종료 시 FadeIn 트리거 (한 번만)
    if (reachedEnd && !m_fadeStarted && m_transitionUIObject && GameInstance::GetInstance()->IsLoadSceneComplete())
    {
        // 천천히 어두워지기(FadeIn = 알파 0 -> 1)
        m_transitionUIObject->FadeIn(m_fadeInDuration, /*onComplete*/[this]()
        {
            m_gameManager->SwitchNextScene();
        });
        m_fadeStarted = true;
    }
}

