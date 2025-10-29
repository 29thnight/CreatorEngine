#include "HPBar.h"
#include "RectTransformComponent.h"
#include "SceneManager.h"
#include "Camera.h"
#include "ImageComponent.h"
#include "Entity.h"
#include "pch.h"

void HPBar::Start()
{
    if (m_target.expired())
    {
        if (GameObject::IsValidIndex(targetIndex))
        {
            if (auto scene = SceneManagers->GetActiveScene())
            {
                if (auto go = scene->TryGetGameObject(targetIndex))
                {
                    m_target = go;
                }
            }
        }
    }

    if (nullptr == m_rect)
    {
        m_rect = m_pOwner->GetComponent<RectTransformComponent>();
	}

    if (nullptr == m_image)
    {
        if (!m_pOwner->m_childrenIndices.empty())
        {
            auto childIndex = m_pOwner->m_childrenIndices.front();
            if (auto childObj = GameObject::FindIndex(childIndex))
            {
                if (auto imageComp = childObj->GetComponent<ImageComponent>())
                {
                    m_image = imageComp;
                    m_image->SetTexture(m_type);
                }

				if (childObj->m_childrenIndices.empty())
                    return;

				int childchildIndex = childObj->m_childrenIndices.front();
                if (auto childchildObj = GameObject::FindIndex(childchildIndex))
                {
                    if (auto warningImageComp = childchildObj->GetComponent<ImageComponent>())
                    {
                        m_warningImage = warningImageComp;
                        m_warningImage->color.w = 0.0f;
					}
                }
            }
        }
    }
}

void HPBar::LateUpdate(float dt)
{
    if (m_target.expired() || nullptr == m_rect || nullptr == m_image)
    {
		GetOwner()->Destroy();
        return;
    }

    auto target_ptr = m_target.lock();
    if (!m_camera)
    {
        auto cameraPtr = CameraManagement->GetLastCamera();
		if (cameraPtr)
        {
            m_camera = cameraPtr.get();
        }
        return;
    }

    auto player = target_ptr->GetComponentDynamicCast<Entity>();
    if (player)
    {
        m_currentHP = player->m_currentHP;
        m_maxHP = player->m_maxHP;
    }

    Mathf::Vector3 worldPos = target_ptr->m_transform.GetWorldPosition();
    auto view = m_camera->CalculateView();
    auto proj = m_camera->CalculateProjection();
    auto viewProj = XMMatrixMultiply(view, proj);

    DirectX::XMVECTOR pos = XMVectorSet(worldPos.x, worldPos.y, worldPos.z, 1.0f);
    DirectX::XMVECTOR clip = XMVector4Transform(pos, viewProj);
    float w = XMVectorGetW(clip);
    if (w <= 0.0f)
        return;

    float x_ndc = XMVectorGetX(clip) / w;
    float y_ndc = XMVectorGetY(clip) / w;

    auto screenSize = m_camera->GetScreenSize();
    float screenX = (x_ndc + 1.0f) * 0.5f * screenSize.width;
    float screenY = (1.0f - y_ndc) * 0.5f * screenSize.height;

    m_rect->SetAnchoredPosition({ screenX + screenOffset.x, screenY + screenOffset.y });

	//hpbar update
	auto ratio = static_cast<float>(m_currentHP) / static_cast<float>(m_maxHP);
	while (ratio < 0.0f) ratio = 0.0f;
	while (ratio > 1.0f) ratio = 1.0f;

	m_image->clipPercent = ratio;

    if (m_warningImage)
    {
        float threshold = std::clamp(m_warningPersent, 0.0f, 1.0f);

        // 기본 targetAlpha: 임계 이하에서 0~1로 선형 증가
        float shortage = std::max(0.0f, threshold - ratio);         // 0 ~ threshold
        float baseAlpha = (threshold > 0.0f) ? (shortage / threshold) : 0.0f; // 0~1

        if (baseAlpha > 0.0f)
        {
            // 블링크 위상 진행
            // 주파수(Hz) -> 각속도 = 2πf
            m_blinkPhase += dt * m_blinkHz;
            if (m_blinkPhase > 1.0f) m_blinkPhase -= std::floor(m_blinkPhase); // 0~1 래핑

            // 사인파(0~1)로 점멸 강도 생성
            // blink01 = 0.5 + 0.5*sin(2π * phase)
            const float twoPi = 6.28318530717958647692f;
            float blink01 = 0.5f + 0.5f * std::sin(twoPi * m_blinkPhase);

            // baseAlpha(부족 정도) * blink(점멸) → [0..1]
            float warnFactor = baseAlpha * blink01;

            // 최종 알파는 [m_minWarnAlpha..m_maxWarnAlpha] 범위로 매핑
            float targetAlpha = std::lerp(m_minWarnAlpha, m_maxWarnAlpha, std::clamp(warnFactor, 0.0f, 1.0f));

            // 부드럽게 따라가고 싶으면(선택): 0.2 가중치 스무딩
            float currentAlpha = m_warningImage->color.w;
            float newAlpha = currentAlpha + (targetAlpha - currentAlpha) * 0.2f;

            m_warningImage->color.w = std::clamp(newAlpha, 0.0f, 1.0f);
        }
        else
        {
            // 임계 위: 천천히 사라지게
            float currentAlpha = m_warningImage->color.w;
            float newAlpha = currentAlpha + (0.0f - currentAlpha) * 0.2f;
            m_warningImage->color.w = std::clamp(newAlpha, 0.0f, 1.0f);
            m_blinkPhase = 0.0f;
        }
    }
}

void HPBar::Init()
{
    m_rect = m_pOwner->GetComponent<RectTransformComponent>();
    if (m_target.expired())
    {
        if (GameObject::IsValidIndex(targetIndex))
        {
            if (auto scene = SceneManagers->GetActiveScene())
            {
                if (auto go = scene->TryGetGameObject(targetIndex))
                {
                    m_target = go;
                }
            }
        }
    }

    if (!m_image)
    {
        if (!m_pOwner->m_childrenIndices.empty())
        {
            auto childIndex = m_pOwner->m_childrenIndices.front();
            if (auto childObj = GameObject::FindIndex(childIndex))
            {
                if (auto imageComp = childObj->GetComponent<ImageComponent>())
                {
                    m_image = imageComp;
                    m_image->SetTexture(m_type);
                }

                if (childObj->m_childrenIndices.empty())
                    return;

                int childchildIndex = childObj->m_childrenIndices.front();
                if (auto childchildObj = GameObject::FindIndex(childchildIndex))
                {
                    if (auto warningImageComp = childchildObj->GetComponent<ImageComponent>())
                    {
                        m_warningImage = warningImageComp;
                        m_warningImage->SetEnabled(false);
                    }
                }
            }
        }
    }
    else
    {
        m_image->SetTexture(m_type);
    }
}

void HPBar::SetType(int type)
{
    m_type = type;
    if (m_image)
    {
        m_image->SetTexture(m_type);
    }
}
