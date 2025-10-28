#pragma once
#include "Core.Minimal.h"
#include "ModuleBehavior.h"

enum class SwitchPhase { Hidden, FadingIn, WaitingInput, FadingOut, Switching };
class GameManager;
class TextComponent;
class SwitchingSceneTrigger : public ModuleBehavior
{
public:
	MODULE_BEHAVIOR_BODY(SwitchingSceneTrigger)
	virtual void Start() override;
	virtual void Update(float tick) override;


	bool IsAnyAJustPressed();
	void SetAlphaAll(float a);

private:
	SwitchPhase m_phase = SwitchPhase::Hidden;
	float m_timer = 0.f;
	// ��ư ���� �����
	bool m_prevA0 = false;
	bool m_prevA1 = false;

	bool m_isFadeInComplete{ false };
	bool m_isFadeOutComplete{ false };
	[[Property]]
	float m_fadeInDuration{ 0.4f }; // ���� ��(������ �ٲ㵵 ��)
	[[Property]]
	float m_fadeOutDuration{ 0.25f }; // ���� ��(������ �ٲ㵵 ��)
	int m_cutsceneIndex{ 0 };
	int m_maxCutsceneIndex{ 3 };
	//[���̵� �� ����]
	//������ spriteFont�� �ؼ� TextComponent�� ������, ���������� ����� ���
	//ImageComponent�� ���� �ʿ�
	GameManager* m_gameManager{ nullptr };
	TextComponent* m_buttonText{ nullptr };
	TextComponent* m_switchingText{ nullptr };
	TextComponent* m_loadingText{ nullptr };
	std::vector<ImageComponent*> m_cutImages{};

};
