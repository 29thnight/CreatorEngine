#pragma once
#include "Core.Minimal.h"
#include "ModuleBehavior.h"
#include "ImageSlideshow.generated.h"

class ImageSlideshow : public ModuleBehavior
{
public:
   ReflectImageSlideshow
	[[ScriptReflectionField]]
	MODULE_BEHAVIOR_BODY(ImageSlideshow)
	virtual void Start() override;
    virtual void FixedUpdate(float fixedTick) override;

public:
    // Controls (������/��ũ��Ʈ ȣ���)
    [[Method]] 
    void Play() { m_playing = true; m_finished = false; }
    [[Method]] 
    void Pause() { m_playing = false; }
    [[Method]] 
    void Stop();                                  // ���⿡ ���� ó��/������ �����ӿ��� ����
    [[Method]] 
    void Next(int step = 1) { StartOrQueueAdvance(step); }    // ��� ����(�Ǵ� step��ŭ)���� �̵�
    [[Method]] 
    void Prev(int step = 1) { StartOrQueueAdvance(-step); }   // ��� ����(�Ǵ� step��ŭ)���� �̵�
    [[Method]] 
    void SetFrame(int index);                     // ���� ���������� ����

    bool IsFinished() const { return m_finished; }

    // Properties
    [[Property]] 
    float m_interval = 0.5f;         // ������ ��ȯ ����(��)
    [[Property]] 
    bool  m_playOnStart{ true };      // ���� �� �ڵ� ���
    [[Property]] 
    bool  m_loop{ false };             // ��ȯ(0..N-1..0..)
    [[Property]] 
    bool  m_pingPong{ false };        // �պ�(0->N-1->0->...)
    [[Property]] 
    bool  m_resetToFirstOnStart{ false }; // ���� �� 0�� ���������� �ʱ�ȭ

    [[Property]] 
    bool  m_fadeEnabled = true;      // ���̵� on/off
    [[Property]] 
    float m_fadeDuration = 0.25f;    // ��ü ���̵� �ð�(��). ������ Out, ������ In
    [[Property]]
    bool  m_stopFadeOnStop = true;
    [[Property]]
    bool  m_stopFadeHoldVisible = false;
    [[Property]]
    float m_stopHoldDuration = 1.0f;

private:
    // ���� ����
    void StartOrQueueAdvance(int step);
    void AdvanceOneStep(int step);     // ���� �ε��� ���+�ݿ� (�� ���ܸ�)
    void ApplyIndex();                 // ImageComponent�� �ε��� ����
    void SetAlpha(float a);            // ImageComponent ���� ���� (API ������ ���� ����)
    float GetAlpha() const;            // ImageComponent ���� �б�

    // ���̵� ����
    enum class FadeState {
        None, FadingOut, SwitchFrame, FadingIn,
        StopFadingOut, StopHold
    };
    void BeginFadeSequence(int stepToAdvance);    // ���̵� ������ ����
    void BeginStopFade();
    void TickFade(float dt);                      // ���̵� ����
	void ImmediateStopToTerminalFrame(); // ��� ���� �� ������ ���������� �̵�

    class ImageComponent* m_image = nullptr;
    float m_elapsed = 0.f;    // FixedUpdate ���� �ð�
    bool  m_playing = false;  // ��� ����
    int   m_direction = 1;    // +1 ������, -1 ������ (���� ��忡 ���)

    // ���̵� ����
    FadeState m_fadeState = FadeState::None;
    float     m_fadeTimer = 0.f;    // ���� ���̵� �ܰ迡�� ��� �ð�
    int       m_pendingStep = 0;    // ���̵� �� ��û�� �̵� step(���� �� ������ �ջ�)
    int       m_switchStep = 0;     // �̹� ��ȯ�� ������ ������ step(���� 1*����)
    bool  m_pendingAutoStop = false;  // ������/ù �����ӿ� �����ؼ�, FadingIn �Ϸ� �� �����ؾ� ��
    float m_stopHoldTimer = 0.f;    // StopHold(����) Ÿ�̸�
    bool  m_finished = false;   // ���� ���� �÷���
};
