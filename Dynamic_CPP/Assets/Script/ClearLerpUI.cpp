#include "ClearLerpUI.h"
#include "ImageComponent.h"
#include "GameInstance.h"
#include "GameManager.h"
#include "pch.h"

void ClearLerpUI::Start()
{
	m_image = GetComponent<ImageComponent>();
    StartFade(2.0f, 1.5f);
	auto gameObj = GameObject::Find("GameManager");
    if (gameObj)
    {
        m_gm = gameObj->GetComponent<GameManager>();
    }

    if (m_gm)
    {
		m_gm->SetLoadingReq(true);
        m_gm->m_nextSceneIndex = (int)SceneType::Credits;
        m_gm->LoadNextScene();
    }
}

void ClearLerpUI::Update(float tick)
{
    // ---- Color Fade (delay → lerp) ----
    if (m_image)
    {
        m_fadeJustDone = false;

        // 1) 딜레이 중
        if (m_fadePending)
        {
            m_fadeDelay -= tick;
            if (m_fadeDelay <= 0.0f)
            {
                m_fadePending = false;
                m_fadeRunning = true;
                m_fadeElapsed = 0.0f;
            }
        }

        // 2) 보간 중
        if (m_fadeRunning)
        {
            if (m_fadeDuration <= 0.0f)
            {
                // duration=0이면 즉시 도달
                m_image->color = Mathf::Vector4{ m_fadeTo.x, m_fadeTo.y, m_fadeTo.z, 1.f };
                m_fadeRunning = false;
                m_fadeJustDone = true;
                OnFadeComplete();
            }
            else
            {
                m_fadeElapsed = std::min(m_fadeElapsed + tick, m_fadeDuration);
                float t = m_fadeElapsed / m_fadeDuration; // 선형
                // 필요 시 ease-in/out 교체 가능 (t*t 등)

                auto c = Mathf::Lerp(m_fadeFrom, m_fadeTo, t);
                m_image->color = Mathf::Vector4{ c.x, c.y, c.z , 1.f };

                if (m_fadeElapsed >= m_fadeDuration)
                {
                    m_fadeRunning = false;
                    m_fadeJustDone = true;     // 이번 프레임 완료 플래그
                    OnFadeComplete();
                }
            }
        }
    }

}

void ClearLerpUI::StartFade(float delaySec, float durationSec, const Mathf::Vector4& from, const Mathf::Vector4& to)
{
    // 파라미터 세팅
    m_fadeDelay = std::max(0.0f, delaySec);
    m_fadeDuration = std::max(0.0f, durationSec);
    m_fadeElapsed = 0.0f;

	m_fadeFrom = Mathf::Vector3{ from.x, from.y, from.z };
	m_fadeTo = Mathf::Vector3{ to.x, to.y, to.z };

    // 상태 초기화
    m_fadePending = (m_fadeDelay > 0.0f);
    m_fadeRunning = !m_fadePending; // 딜레이 0이면 바로 시작
    m_fadeJustDone = false;

    // 시작 시점 컬러 적용
    if (m_image)
    {
		m_image->color = Mathf::Vector4{ m_fadeFrom.x, m_fadeFrom.y, m_fadeFrom.z, 1.f };
    }
}

void ClearLerpUI::OnFadeComplete()
{
    if (m_gm)
    {
        m_gm->SwitchNextSceneWithFade();
    }
}

