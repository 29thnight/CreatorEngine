#pragma once
#include "Core.Minimal.h"
#include "ModuleBehavior.h"

class SoundBarUI : public ModuleBehavior
{
public:
	MODULE_BEHAVIOR_BODY(SoundBarUI)
	virtual void Start() override;
	virtual void Update(float tick) override;

    // 0~100 ���� �ۼ�Ʈ�� ����/��ȸ
    void SetVolumePercent(int percent);
    int  GetVolumePercent() const { return m_percent; }

private:
    // �ʱ� ���� ���
    void   RecalculateBarRange();
    // �ۼ�Ʈ -> ��ư ��ġ
    float  CalcButtonXFromPercent(int percent) const;
    // ��ư ��ġ -> �ۼ�Ʈ (�ʿ� �� ���)
    int    CalcPercentFromButtonX(float x) const;

private:
    // ��� ĳ��
    float m_barCenterX = 0.f;    // ��(���� ��) �߽�
    float m_barHalfW = 0.f;    // �� ���� �ʺ�
    float m_btnHalfW = 0.f;    // ��ư ���� �ʺ�
    float m_minX = 0.f;    // ��ư�� �� �� �ִ� �ּ� X (0%)
    float m_maxX = 0.f;    // ��ư�� �� �� �ִ� �ִ� X (100%)
    float m_stepX = 0.f;    // 1%�� �ش��ϴ� �Ÿ�

    int   m_percent = 100;    // ���� �ۼ�Ʈ(0~100)

    float m_deadzone = 0.25f;     // ��ƽ ������
    float m_unitsPerSec = 80.f;   // �ʴ� �ۼ�Ʈ ��ȭ��(������ ���� ���� ����ȭ ����)

private:
	class ImageComponent* m_soundBarImageComponent{ nullptr };
	class ImageComponent* m_soundBarButtonImageComponent{ nullptr };
	class RectTransformComponent* m_soundBarButtonRect{ nullptr };
	class RectTransformComponent* m_soundBarRect{ nullptr };
};
