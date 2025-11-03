#pragma once
#include "Core.Minimal.h"
#include "ModuleBehavior.h"
#include "CreditScroll.generated.h"

class RectTransformComponent;
class SceneTransitionUI;
class GameManager;
class CreditScroll : public ModuleBehavior
{
public:
   ReflectCreditScroll
	[[ScriptReflectionField]]
	MODULE_BEHAVIOR_BODY(CreditScroll)
	virtual void Start() override;
	virtual void Update(float tick) override;

private:
    [[Property]] 
    float m_scrollSpeed = 60.f;  // 기본 속도(픽셀/초 기준이면 엔진 tick에 맞게 조정)
    [[Property]] 
    float m_fastMultiplier = 6.0f;  // Y키 가속배수
    [[Property]] 
    float m_startY = 2100.f; // 시작 Y
    [[Property]] 
    float m_endY = -910.f; // 도착 Y

    [[Property]] 
    float m_fadeInDuration = 1.0f;    // 스크롤 끝난 뒤 페이드 인 시간(초)
    [[Property]] 
    bool  m_triggerFadeOnce = true;    // true면 한 번만 트리거

private:
	GameManager*            m_gameManager = nullptr;
    RectTransformComponent* m_rect = nullptr;
    SceneTransitionUI*      m_transitionUIObject = nullptr;
    bool                    m_fadeStarted = false;  // 중복 호출 방지
};
