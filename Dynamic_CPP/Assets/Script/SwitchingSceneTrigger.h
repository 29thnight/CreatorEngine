#pragma once
#include "Core.Minimal.h"
#include "ModuleBehavior.h"
#include "SwitchingSceneTrigger.generated.h"

enum class SwitchPhase { Hidden, FadingIn, WaitingInput, FadingOut, Switching };
class GameManager;
class TextComponent;
class SwitchingSceneTrigger : public ModuleBehavior
{
public:
   ReflectSwitchingSceneTrigger
	[[ScriptReflectionField]]
	MODULE_BEHAVIOR_BODY(SwitchingSceneTrigger)
	virtual void Start() override;
	virtual void Update(float tick) override;


	bool IsAnyAJustPressed();
	void SetAlphaAll(float a);

private:
	SwitchPhase m_phase = SwitchPhase::Hidden;
	float m_timer = 0.f;
	// 버튼 에지 검출용
	bool m_prevA0 = false;
	bool m_prevA1 = false;
	[[Property]]
	bool m_isTestMode{ false };
	[[Property]]
	bool m_isTestBossStage{ false };

	bool m_isFadeInComplete{ false };
	bool m_isFadeOutComplete{ false };
	[[Property]]
	float m_fadeInDuration{ 0.4f }; // 연출 값(취향대로 바꿔도 됨)
	[[Property]]
	float m_fadeOutDuration{ 0.25f }; // 연출 값(취향대로 바꿔도 됨)
	int m_cutsceneIndex{ 0 };
	int m_maxCutsceneIndex{ 3 };
	//[페이드 인 전용]
	//지금은 spriteFont로 해서 TextComponent를 받지만, 아이콘으로 변경될 경우
	//ImageComponent로 변경 필요
	GameManager*					m_gameManager{ nullptr };
	TextComponent*					m_buttonText{ nullptr };
	TextComponent*					m_switchingText{ nullptr };
	TextComponent*					m_loadingText{ nullptr };
	std::vector<ImageComponent*>	m_cutImages{};

	int   m_cutStart = 0;   // 포함
	int   m_cutEndExclusive = 0;   // 미포함
	int   m_cutCursor = 0;   // 현재 재생 위치
	bool  m_cutRangeReady = false;
	// 자동 진행
	[[Property]]
	float   m_autoPlayDelay = 3.2f;   // 컷신 자동 진행 간격(초) - 원하는 값으로
	float   m_autoTimer = 0.0f;   // 누적 타이머
	[[Property]]
	bool    m_waitAtLastCut = true;   // 마지막 컷에서는 입력 대기

	void  SetupCutRangeForNextScene();
	void ShowCut(int idx);
};
