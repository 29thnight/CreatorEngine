#include "HPBar.h"
#include "RectTransformComponent.h"
#include "SceneManager.h"
#include "Camera.h"
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
                m_target = go.get();
            }
        }
    }
}

void HPBar::LateUpdate(float)
{
    if (nullptr == m_target || nullptr == m_rect)
    {
        m_rect = m_pOwner->GetComponent<RectTransformComponent>();
        if (GameObject::IsValidIndex(targetIndex))
        {
            if (auto scene = SceneManagers->GetActiveScene())
            {
                if (auto go = scene->GetGameObject(targetIndex))
                {
                    m_target = go.get();
                }
            }
        }
        return;
    }

    auto cameraPtr = CameraManagement->GetLastCamera();
    if (!cameraPtr)
        return;
    Camera* camera = cameraPtr.get();

    Mathf::Vector3 worldPos = m_target->m_transform.GetWorldPosition();
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
}

