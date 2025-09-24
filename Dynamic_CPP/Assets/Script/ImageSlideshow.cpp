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

    // ���� ���Ĵ� 1�� ����δ� �� ����
    if (m_fadeEnabled) SetAlpha(1.f);
}

void ImageSlideshow::FixedUpdate(float fixedTick)
{
    if (!m_image) return;

    // 1) ���̵� ���¸� �ֿ켱���� ó��
    if (m_fadeEnabled && m_fadeState != FadeState::None) 
    {
        TickFade(fixedTick);
        return; // ���̵� �߿��� �ٸ� ó�� �� ��
    }

    // 2) �� �������� �����̵� ��� �˻�
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
            if (!m_playing) break; // Stop�Ǹ� Ż��
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
        // ���̵� ���̸� ��û�� �����ؼ� ���� SwitchFrame �� ����
        m_pendingStep += step;
        if (m_fadeState == FadeState::None) {
            // ���̵尡 �����ִ� ���¶�� �ٷ� ���̵� ����
            BeginFadeSequence((step == 0) ? (1 * m_direction) : step);
        }
    }
    else {
        // ��� ����
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
        // ���� ���� ���(Stop �� ������/ó�� ���ÿ�)
        if (step != 0) m_direction = (step >= 0 ? 1 : -1);

        // �ٽ�: ��� Stop ���̵� ���� X
        // ������ �������� ������ ������ ��(=FadingIn �Ϸ� ��) ������ ���� ����
        m_pendingAutoStop = true;
        // ��� ������ ���缭 ���� ���͹��� �������� �ʵ���
        m_playing = false;
        return;
    }
}

void ImageSlideshow::ApplyIndex()
{
    // �ε��� ���� �� ���� �ؽ�ó/������/���� ����
    m_image->UpdateTexture();
}

void ImageSlideshow::Stop()
{
    // �̹� Stop ���̵� ���̸� Ÿ�̸� ���� ���� (����)
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
        // ���̵� ����: ��� ��ȯ
        AdvanceOneStep(stepToAdvance);
        return;
    }

    // �̹� ���� ���̸� ���� step�� ��Ƶΰ�, ���� ���̵�� �״�� ����
    if (m_fadeState != FadeState::None) {
        m_pendingStep += stepToAdvance;
        return;
    }

    // �� ���̵� ����
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
            // ������ ��ȯ ����
            m_fadeState = FadeState::SwitchFrame;
            m_fadeTimer = 0.f;
        }
        break;
    }
    case FadeState::SwitchFrame:
    {
        // ��û�� ���� + ���̵� �� ������ ������ �ջ��� �� ���� ����
        int step = m_switchStep + m_pendingStep;
        m_pendingStep = 0;

        AdvanceOneStep(step);

        // ��ȯ �� In ����
        m_fadeState = FadeState::FadingIn;
        m_fadeTimer = 0.f;

        // ��ȯ ���� ���� 0 ����
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
        //    // ���̵� �Ϸ�
        //    m_fadeState = FadeState::None;
        //    m_fadeTimer = 0.f;
        //}
        if (m_fadeTimer >= half) {
            // ���̵� �� �Ϸ�
            m_fadeTimer = 0.f;

            if (m_pendingAutoStop && m_fadeEnabled && m_stopFadeOnStop) {
                // ������ �������� ����� �����ֱ� ���� Hold �ܰ�� ����(�ɼ�)
                if (m_stopHoldDuration > 0.f) {
                    m_fadeState = FadeState::StopHold;
                    m_stopHoldTimer = 0.f;
                    // ���Ĵ� 1.0���� ����
                    SetAlpha(1.f);
                }
                else {
                    // Hold ���� �ٷ� StopFadingOut
                    m_fadeState = FadeState::StopFadingOut;
                }
            }
            else {
                // �Ϲ� ���̽�: ���̵� ���� ����
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
            m_fadeState = FadeState::StopFadingOut; // ���� ���̵� �ƿ� ����
        }
        // ���Ĵ� 1.0 ����
        SetAlpha(1.f);
        break;
    }
    case FadeState::StopFadingOut:
    {
        // ��ü m_fadeDuration�� �ƿ��� ��� ��� (�ε巴�� ��Ӱ�)
        m_fadeTimer += dt;
        float t = (m_fadeDuration > 0.f) ? (m_fadeTimer / m_fadeDuration) : 1.f;
        if (t > 1.f) t = 1.f;

        // 1 -> 0
        SetAlpha(1.f - t);

        if (m_fadeTimer >= m_fadeDuration) {
            // ������ ����� ������ ���⿡ �´� ���� ���������� �̵�
            if (m_image) {
                const int count = static_cast<int>(m_image->GetTextures().size());
                if (count > 0) {
                    const int target = (m_direction >= 0) ? (count - 1) : 0;
                    SetFrame(target);
                }
            }

            // ���� ���� Ȯ��
            m_playing = false;
            m_elapsed = 0.f;
            m_pendingStep = 0;
            m_switchStep = 0;

            // �������� ������ or ���̰� ����
            if (m_stopFadeHoldVisible) {
                SetAlpha(1.f);  // ���̰� ����
            }
            else {
                SetAlpha(0.f);  // �����ϰ� ����
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
        // Stop() �ٽ� �θ��� ���� �ٷ� ��� ���� ó��
        ImmediateStopToTerminalFrame();
        return;
    }
    // �̹� StopFadingOut ���̸� Ÿ�̸Ӹ� �������� �ʵ��� ����(����)
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

    // Stop �� ���̰�/�����ϰ� ������
    if (m_fadeEnabled && !m_stopFadeHoldVisible) SetAlpha(0.f);
    else SetAlpha(1.f);
}