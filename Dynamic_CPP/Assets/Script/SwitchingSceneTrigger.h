#pragma once
#include "Core.Minimal.h"
#include "ModuleBehavior.h"

enum class SwitchPhase { Hidden, FadingIn, WaitingInput, FadingOut, Switching };

class SwitchingSceneTrigger : public ModuleBehavior
{
public:
	MODULE_BEHAVIOR_BODY(SwitchingSceneTrigger)
	virtual void Start() override;
	virtual void Update(float tick) override;


	bool IsAnyAJustPressed();
	void SetAlphaAll(float a) {
		if (m_buttonText)    m_buttonText->SetAlpha(a);
		if (m_switchingText) m_switchingText->SetAlpha(a);
	}

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
	//[���̵� �� ����]
	//������ spriteFont�� �ؼ� TextComponent�� ������, ���������� ����� ���
	//ImageComponent�� ���� �ʿ�
	class GameManager* m_gameManager{ nullptr };
	class TextComponent* m_buttonText{ nullptr };
	class TextComponent* m_switchingText{ nullptr };
};
