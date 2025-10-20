#pragma once
#include "Core.Minimal.h"
#include "ModuleBehavior.h"

class SoundBarUI : public ModuleBehavior
{
public:
	MODULE_BEHAVIOR_BODY(SoundBarUI)
	virtual void Start() override;
	virtual void Update(float tick) override;

    // 0~100 정수 퍼센트로 설정/조회
    void SetVolumePercent(int percent);
    int  GetVolumePercent() const { return m_percent; }

private:
    // 초기 범위 계산
    void   RecalculateBarRange();
    // 퍼센트 -> 버튼 위치
    float  CalcButtonXFromPercent(int percent) const;
    // 버튼 위치 -> 퍼센트 (필요 시 사용)
    int    CalcPercentFromButtonX(float x) const;

private:
    // 계산 캐시
    float m_barCenterX = 0.f;    // 바(사운드 바) 중심
    float m_barHalfW = 0.f;    // 바 절반 너비
    float m_btnHalfW = 0.f;    // 버튼 절반 너비
    float m_minX = 0.f;    // 버튼이 갈 수 있는 최소 X (0%)
    float m_maxX = 0.f;    // 버튼이 갈 수 있는 최대 X (100%)
    float m_stepX = 0.f;    // 1%에 해당하는 거리

    int   m_percent = 100;    // 현재 퍼센트(0~100)

    float m_deadzone = 0.25f;     // 스틱 데드존
    float m_unitsPerSec = 80.f;   // 초당 퍼센트 변화량(데드존 이후 구간 정규화 적용)

private:
	class ImageComponent* m_soundBarImageComponent{ nullptr };
	class ImageComponent* m_soundBarButtonImageComponent{ nullptr };
	class RectTransformComponent* m_soundBarButtonRect{ nullptr };
	class RectTransformComponent* m_soundBarRect{ nullptr };
};
