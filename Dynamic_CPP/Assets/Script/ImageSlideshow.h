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
    // Controls (에디터/스크립트 호출용)
    [[Method]] 
    void Play() { m_playing = true; m_finished = false; }
    [[Method]] 
    void Pause() { m_playing = false; }
    [[Method]] 
    void Stop();                                  // 방향에 따라 처음/마지막 프레임에서 정지
    [[Method]] 
    void Next(int step = 1) { StartOrQueueAdvance(step); }    // 즉시 다음(또는 step만큼)으로 이동
    [[Method]] 
    void Prev(int step = 1) { StartOrQueueAdvance(-step); }   // 즉시 이전(또는 step만큼)으로 이동
    [[Method]] 
    void SetFrame(int index);                     // 임의 프레임으로 점프

    bool IsFinished() const { return m_finished; }

    // Properties
    [[Property]] 
    float m_interval = 0.5f;         // 프레임 전환 간격(초)
    [[Property]] 
    bool  m_playOnStart{ true };      // 시작 시 자동 재생
    [[Property]] 
    bool  m_loop{ false };             // 순환(0..N-1..0..)
    [[Property]] 
    bool  m_pingPong{ false };        // 왕복(0->N-1->0->...)
    [[Property]] 
    bool  m_resetToFirstOnStart{ false }; // 시작 시 0번 프레임으로 초기화

    [[Property]] 
    bool  m_fadeEnabled = true;      // 페이드 on/off
    [[Property]] 
    float m_fadeDuration = 0.25f;    // 전체 페이드 시간(초). 절반은 Out, 절반은 In
    [[Property]]
    bool  m_stopFadeOnStop = true;
    [[Property]]
    bool  m_stopFadeHoldVisible = false;
    [[Property]]
    float m_stopHoldDuration = 1.0f;

private:
    // 내부 동작
    void StartOrQueueAdvance(int step);
    void AdvanceOneStep(int step);     // 실제 인덱스 계산+반영 (한 스텝만)
    void ApplyIndex();                 // ImageComponent에 인덱스 적용
    void SetAlpha(float a);            // ImageComponent 알파 적용 (API 유무에 따라 구현)
    float GetAlpha() const;            // ImageComponent 알파 읽기

    // 페이드 상태
    enum class FadeState {
        None, FadingOut, SwitchFrame, FadingIn,
        StopFadingOut, StopHold
    };
    void BeginFadeSequence(int stepToAdvance);    // 페이드 시퀀스 시작
    void BeginStopFade();
    void TickFade(float dt);                      // 페이드 진행
	void ImmediateStopToTerminalFrame(); // 즉시 정지 시 마지막 프레임으로 이동

    class ImageComponent* m_image = nullptr;
    float m_elapsed = 0.f;    // FixedUpdate 누적 시간
    bool  m_playing = false;  // 재생 여부
    int   m_direction = 1;    // +1 정방향, -1 역방향 (핑퐁 모드에 사용)

    // 페이드 제어
    FadeState m_fadeState = FadeState::None;
    float     m_fadeTimer = 0.f;    // 현재 페이드 단계에서 경과 시간
    int       m_pendingStep = 0;    // 페이드 중 요청된 이동 step(여러 번 눌러도 합산)
    int       m_switchStep = 0;     // 이번 전환에 실제로 적용할 step(보통 1*방향)
    bool  m_pendingAutoStop = false;  // 마지막/첫 프레임에 도달해서, FadingIn 완료 후 정지해야 함
    float m_stopHoldTimer = 0.f;    // StopHold(유지) 타이머
    bool  m_finished = false;   // 완전 종료 플래그
};
