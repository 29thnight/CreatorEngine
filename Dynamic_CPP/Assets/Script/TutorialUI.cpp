#include "TutorialUI.h"
#include "pch.h"
#include "RectTransformComponent.h"
#include "Camera.h"
#include "ImageComponent.h"
void TutorialUI::Start()
{
    if (nullptr == m_rect)
    {
        m_rect = m_pOwner->GetComponent<RectTransformComponent>();
    }
    if (nullptr == m_rect)
    {
        m_image = m_pOwner->GetComponent<ImageComponent>();
    }

}

void TutorialUI::Update(float tick)
{
}

void TutorialUI::LateUpdate(float tick)
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



}

void TutorialUI::Init()
{
    m_rect = m_pOwner->GetComponent<RectTransformComponent>();

    if (nullptr == m_image)
    {
        m_image = m_pOwner->GetComponent<ImageComponent>();
    }
    else
    {
        m_image->SetTexture(m_type);
    }

}

void TutorialUI::SetType(int type)
{
    m_type = type;
    if (m_image)
    {
        m_image->SetTexture(m_type);
    }
}

