#pragma once
#include "Core.Minimal.h"
#include "ModuleBehavior.h"

class ViveSwitchUI : public ModuleBehavior
{
public:
	MODULE_BEHAVIOR_BODY(ViveSwitchUI)
	virtual void Start() override;
	virtual void Update(float tick) override;

	void SetViveEnabled(bool enable);
	void ToggleViveEnabled();
	bool IsViveEnabled() const { return m_isViveEnabled; }

private:
	void RecalculatePositions(); // �߾ӡ�1/4 ��ġ ���
	void ApplyButton();          // ���� ���¿� �°� ��ư ��ġ

private:
	class RectTransformComponent*	m_barRect = nullptr;
	class ImageComponent*			m_barImage = nullptr;
	class RectTransformComponent*	m_btnRect = nullptr;
	class ImageComponent*			m_btnImage = nullptr;

	bool  m_isViveEnabled = false;

	// ��� ĳ��
	float m_centerX = 0.f;      // �� �߽� x
	float m_barHalfW = 0.f;     // �� ���� ��
	float m_btnHalfW = 0.f;     // ��ư ���� ��
	float m_offX = 0.f;         // �߾� - (������ 1/4), Ŭ���� �ݿ�
	float m_onX = 0.f;         // �߾� + (������ 1/4), Ŭ���� �ݿ�

	// (����) �ִϸ��̼�
	bool  m_useSmoothMove = true;
	float m_moveSpeed = 24.f;   // Ŀ��/�� ���� ���� �ӵ�

	float m_toggleThreshold = 0.20f; // �� �̻� �������̸� ON, �����̸� OFF
	float m_cooldownSec = 0.20f; // ��Ÿ ���� ��ٿ�
	float m_cdTimer = 0.f;
};
