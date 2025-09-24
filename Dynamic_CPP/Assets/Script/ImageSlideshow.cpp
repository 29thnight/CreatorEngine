#include "ImageSlideshow.h"
#include "ImageComponent.h"
#include "pch.h"
void ImageSlideshow::Start()
{
    m_image = GetOwner() ? GetOwner()->GetComponent<ImageComponent>() : nullptr;

    m_playing = m_playOnStart;
    m_elapsed = 0.f;
    m_fadeState = FadeState::None;
    m_fadeTimer = 0.f;
    m_pendingStep = 0;
    m_switchStep = 0;

    if (m_resetToFirstOnStart && m_image)
        SetFrame(0);

    // 시작 알파는 1로 맞춰두는 걸 권장
    if (m_fadeEnabled) SetAlpha(1.f);
}

void ImageSlideshow::FixedUpdate(float fixedTick)
{
    if (!m_image) return;

    // 1) 페이드 상태를 최우선으로 처리
    if (m_fadeEnabled && m_fadeState != FadeState::None) 
    {
        TickFade(fixedTick);
        return; // 페이드 중에는 다른 처리 안 함
    }

    // 2) 그 다음에야 슬라이드 요건 검사
    const auto& textures = m_image->GetTextures();
    const int count = static_cast<int>(textures.size());
    if (count <= 1) return;

    if (m_fadeEnabled) 
    {
        if (!m_playing || m_interval <= 0.f) return;
        m_elapsed += fixedTick;
        if (m_elapsed >= m_interval) 
        {
            m_elapsed -= m_interval;
            BeginFadeSequence(1 * m_direction);
        }
    }
    else 
    {
        if (!m_playing || m_interval <= 0.f) return;
        m_elapsed += fixedTick;
        while (m_elapsed >= m_interval) 
        {
            m_elapsed -= m_interval;
            AdvanceOneStep(1 * m_direction);
            if (!m_playing) break; // Stop되면 탈출
        }
    }
}

void ImageSlideshow::SetFrame(int index)
{
    if (!m_image) return;
    const int count = static_cast<int>(m_image->GetTextures().size());
    if (count <= 0) return;

    if (index < 0) index = 0;
    if (index >= count) index = count - 1;

    if (index != m_image->curindex) {
        m_image->curindex = index;
        ApplyIndex();
    }
}

void ImageSlideshow::StartOrQueueAdvance(int step)
{
    if (!m_image) return;

    if (m_fadeEnabled) {
        // 페이드 중이면 요청을 누적해서 다음 SwitchFrame 때 적용
        m_pendingStep += step;
        if (m_fadeState == FadeState::None) {
            // 페이드가 꺼져있는 상태라면 바로 페이드 시작
            BeginFadeSequence((step == 0) ? (1 * m_direction) : step);
        }
    }
    else {
        // 즉시 진행
        AdvanceOneStep(step);
    }
}

void ImageSlideshow::AdvanceOneStep(int step)
{
    const int count = static_cast<int>(m_image->GetTextures().size());
    if (count <= 0) return;

    int idx = m_image->curindex + step;

    if (m_pingPong) {
        if (idx >= count) {
            idx = count - 2;
            m_direction = -1;
        }
        else if (idx < 0) {
            idx = 1;
            m_direction = 1;
        }
    }
    else if (m_loop) {
        idx %= count;
        if (idx < 0) idx += count;
    }
    else {
        if (idx >= count) idx = count - 1;
        if (idx < 0) idx = 0;
    }

    const bool reachedTerminal =
        (!m_loop && !m_pingPong) &&
        ((step >= 0 && idx == count - 1) || (step <= 0 && idx == 0));

    if (idx != m_image->curindex) {
        m_image->curindex = idx;
        ApplyIndex();
    }

    if (reachedTerminal) {
        // 현재 방향 기록(Stop 시 마지막/처음 선택용)
        if (step != 0) m_direction = (step >= 0 ? 1 : -1);

        // 핵심: 즉시 Stop 페이드 시작 X
        // 마지막 프레임을 완전히 보여준 뒤(=FadingIn 완료 후) 정지할 것을 예약
        m_pendingAutoStop = true;
        // 재생 루프는 멈춰서 다음 인터벌을 시작하지 않도록
        m_playing = false;
        return;
    }
}

void ImageSlideshow::ApplyIndex()
{
    // 인덱스 변경 시 실제 텍스처/사이즈/원점 갱신
    m_image->UpdateTexture();
}

void ImageSlideshow::Stop()
{
    // 이미 Stop 페이드 중이면 타이머 리셋 방지 (선택)
    if (m_fadeEnabled && m_stopFadeOnStop && m_fadeDuration > 0.f) {
        if (m_fadeState != FadeState::StopFadingOut) {
            BeginStopFade();
        }
        return;
    }
    ImmediateStopToTerminalFrame();
}

void ImageSlideshow::BeginFadeSequence(int stepToAdvance)
{
    if (!m_fadeEnabled || m_fadeDuration <= 0.f) {
        // 페이드 꺼짐: 즉시 전환
        AdvanceOneStep(stepToAdvance);
        return;
    }

    // 이미 진행 중이면 누적 step에 담아두고, 현재 페이드는 그대로 진행
    if (m_fadeState != FadeState::None) {
        m_pendingStep += stepToAdvance;
        return;
    }

    // 새 페이드 시작
    m_switchStep = stepToAdvance;
    m_fadeState = FadeState::FadingOut;
    m_fadeTimer = 0.f;
}

void ImageSlideshow::TickFade(float dt)
{
    const float half = m_fadeDuration * 0.5f;
    switch (m_fadeState)
    {
    case FadeState::FadingOut:
    {
        m_fadeTimer += dt;
        float t = (half > 0.f) ? (m_fadeTimer / half) : 1.f;
        if (t > 1.f) t = 1.f;

        // Out: 1 -> 0
        SetAlpha(1.f - t);

        if (m_fadeTimer >= half) {
            // 프레임 전환 시점
            m_fadeState = FadeState::SwitchFrame;
            m_fadeTimer = 0.f;
        }
        break;
    }
    case FadeState::SwitchFrame:
    {
        // 요청된 스텝 + 페이드 중 누적된 스텝을 합산해 한 번에 적용
        int step = m_switchStep + m_pendingStep;
        m_pendingStep = 0;

        AdvanceOneStep(step);

        // 전환 후 In 진행
        m_fadeState = FadeState::FadingIn;
        m_fadeTimer = 0.f;

        // 전환 직후 알파 0 보장
        SetAlpha(0.f);
        break;
    }
    case FadeState::FadingIn:
    {
        m_fadeTimer += dt;
        float t = (half > 0.f) ? (m_fadeTimer / half) : 1.f;
        if (t > 1.f) t = 1.f;

        // In: 0 -> 1
        SetAlpha(t);

        //if (m_fadeTimer >= half) {
        //    // 페이드 완료
        //    m_fadeState = FadeState::None;
        //    m_fadeTimer = 0.f;
        //}
        if (m_fadeTimer >= half) {
            // 페이드 인 완료
            m_fadeTimer = 0.f;

            if (m_pendingAutoStop && m_fadeEnabled && m_stopFadeOnStop) {
                // 마지막 프레임을 충분히 보여주기 위한 Hold 단계로 진입(옵션)
                if (m_stopHoldDuration > 0.f) {
                    m_fadeState = FadeState::StopHold;
                    m_stopHoldTimer = 0.f;
                    // 알파는 1.0으로 유지
                    SetAlpha(1.f);
                }
                else {
                    // Hold 없이 바로 StopFadingOut
                    m_fadeState = FadeState::StopFadingOut;
                }
            }
            else {
                // 일반 케이스: 페이드 상태 종료
                m_fadeState = FadeState::None;
            }
        }
        break;
    }
    case FadeState::StopHold:
    {
        m_stopHoldTimer += dt;
        if (m_stopHoldTimer >= m_stopHoldDuration) {
            m_stopHoldTimer = 0.f;
            m_fadeState = FadeState::StopFadingOut; // 이제 페이드 아웃 시작
        }
        // 알파는 1.0 유지
        SetAlpha(1.f);
        break;
    }
    case FadeState::StopFadingOut:
    {
        // 전체 m_fadeDuration을 아웃에 모두 사용 (부드럽게 어둡게)
        m_fadeTimer += dt;
        float t = (m_fadeDuration > 0.f) ? (m_fadeTimer / m_fadeDuration) : 1.f;
        if (t > 1.f) t = 1.f;

        // 1 -> 0
        SetAlpha(1.f - t);

        if (m_fadeTimer >= m_fadeDuration) {
            // 완전히 사라진 시점에 방향에 맞는 정지 프레임으로 이동
            if (m_image) {
                const int count = static_cast<int>(m_image->GetTextures().size());
                if (count > 0) {
                    const int target = (m_direction >= 0) ? (count - 1) : 0;
                    SetFrame(target);
                }
            }

            // 정지 상태 확정
            m_playing = false;
            m_elapsed = 0.f;
            m_pendingStep = 0;
            m_switchStep = 0;

            // 투명으로 마무리 or 보이게 멈춤
            if (m_stopFadeHoldVisible) {
                SetAlpha(1.f);  // 보이게 정지
            }
            else {
                SetAlpha(0.f);  // 투명하게 정지
            }

            m_fadeState = FadeState::None;
            m_fadeTimer = 0.f;
        }
        break;
    }
    default:
        m_fadeState = FadeState::None;
        break;
    }
}

void ImageSlideshow::SetAlpha(float a)
{
    if (m_image) 
    {
        float& alpha = m_image->color.w;
        alpha = std::clamp(a, 0.f, 1.f);
    }
}

float ImageSlideshow::GetAlpha() const
{
    if (m_image) 
    {
        return m_image->color.w;
    }
    return 1.f;
}

void ImageSlideshow::BeginStopFade()
{
    if (!m_fadeEnabled || m_fadeDuration <= 0.f) {
        // Stop() 다시 부르지 말고 바로 즉시 정지 처리
        ImmediateStopToTerminalFrame();
        return;
    }
    // 이미 StopFadingOut 중이면 타이머만 리셋하지 않도록 가드(선택)
    if (m_fadeState != FadeState::StopFadingOut) {
        m_fadeState = FadeState::StopFadingOut;
        m_fadeTimer = 0.f;
    }
    m_playing = false;
}

void ImageSlideshow::ImmediateStopToTerminalFrame()
{
    m_playing = false;
    m_elapsed = 0.f;
    m_pendingStep = 0;
    m_switchStep = 0;

    if (m_image) {
        const int count = static_cast<int>(m_image->GetTextures().size());
        if (count > 0) {
            const int target = (m_direction >= 0) ? (count - 1) : 0;
            SetFrame(target);
        }
    }

    m_fadeState = FadeState::None;
    m_fadeTimer = 0.f;

    // Stop 시 보이게/투명하게 멈출지
    if (m_fadeEnabled && !m_stopFadeHoldVisible) SetAlpha(0.f);
    else SetAlpha(1.f);
}