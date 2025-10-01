#include "HPBar.h"
#include "RectTransformComponent.h"
#include "SceneManager.h"
#include "Camera.h"
#include "ImageComponent.h"
#include "Player.h"
#include "pch.h"

void HPBar::Start()
{
    m_rect = m_pOwner->GetComponent<RectTransformComponent>();
    if (GameObject::IsValidIndex(targetIndex))
    {
        if (auto scene = SceneManagers->GetActiveScene())
        {
            if (auto go = scene->GetGameObject(targetIndex))
            {
                m_target = go;
            }
        }
    }

	auto childIndex = m_pOwner->m_childrenIndices[0];
    auto childObj = GameObject::FindIndex(childIndex);
    auto imageComp = childObj->GetComponent<ImageComponent>();
    if (imageComp)
    {
        m_image = imageComp;
        m_image->SetTexture(m_type);
    }
}

void HPBar::LateUpdate(float)
{
    if (m_target.expired() || nullptr == m_rect || nullptr == m_image)
    {
        m_rect = m_pOwner->GetComponent<RectTransformComponent>();
        if (GameObject::IsValidIndex(targetIndex))
        {
            if (auto scene = SceneManagers->GetActiveScene())
            {
                if (auto go = scene->GetGameObject(targetIndex))
                {
                    m_target = go;
                }
            }
        }

        auto childIndex = m_pOwner->m_childrenIndices[0];
        auto childObj = GameObject::FindIndex(childIndex);
        auto imageComp = childObj->GetComponent<ImageComponent>();
        if (imageComp)
        {
            m_image = imageComp;
            m_image->SetTexture(m_type);
        }
        return;
    }

    auto target_ptr = m_target.lock();
    //rectTransform update
    auto cameraPtr = CameraManagement->GetLastCamera();
    if (!cameraPtr || !target_ptr)
        return;
    Camera* camera = cameraPtr.get();

    auto player = target_ptr->GetComponentDynamicCast<Entity>();
    if (player)
    {
        m_currentHP = player->m_currentHP;
        m_maxHP = player->m_maxHP;
    }

    Mathf::Vector3 worldPos = target_ptr->m_transform.GetWorldPosition();
    auto view = camera->CalculateView();
    auto proj = camera->CalculateProjection();
    auto viewProj = XMMatrixMultiply(view, proj);

    DirectX::XMVECTOR pos = XMVectorSet(worldPos.x, worldPos.y, worldPos.z, 1.0f);
    DirectX::XMVECTOR clip = XMVector4Transform(pos, viewProj);
    float w = XMVectorGetW(clip);
    if (w <= 0.0f)
        return;

    float x_ndc = XMVectorGetX(clip) / w;
    float y_ndc = XMVectorGetY(clip) / w;

    auto screenSize = camera->GetScreenSize();
    float screenX = (x_ndc + 1.0f) * 0.5f * screenSize.width;
    float screenY = (1.0f - y_ndc) * 0.5f * screenSize.height;

    m_rect->SetAnchoredPosition({ screenX + screenOffset.x, screenY + screenOffset.y });

	//hpbar update
	auto ratio = static_cast<float>(m_currentHP) / static_cast<float>(m_maxHP);
	while (ratio < 0.0f) ratio = 0.0f;
	while (ratio > 1.0f) ratio = 1.0f;

	m_image->clipPercent = ratio;
}

void HPBar::SetType(int type)
{
    m_type = type;
}
