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
	void RecalculatePositions(); // 중앙±1/4 위치 계산
	void ApplyButton();          // 현재 상태에 맞게 버튼 배치

private:
	class RectTransformComponent*	m_barRect = nullptr;
	class ImageComponent*			m_barImage = nullptr;
	class RectTransformComponent*	m_btnRect = nullptr;
	class ImageComponent*			m_btnImage = nullptr;

	bool  m_isViveEnabled = false;

	// 계산 캐시
	float m_centerX = 0.f;      // 바 중심 x
	float m_barHalfW = 0.f;     // 바 절반 폭
	float m_btnHalfW = 0.f;     // 버튼 절반 폭
	float m_offX = 0.f;         // 중앙 - (바폭의 1/4), 클램프 반영
	float m_onX = 0.f;         // 중앙 + (바폭의 1/4), 클램프 반영

	// (선택) 애니메이션
	bool  m_useSmoothMove = true;
	float m_moveSpeed = 24.f;   // 커서/초 기준 보간 속도

	float m_toggleThreshold = 0.20f; // 이 이상 오른쪽이면 ON, 왼쪽이면 OFF
	float m_cooldownSec = 0.20f; // 연타 방지 쿨다운
	float m_cdTimer = 0.f;
};
