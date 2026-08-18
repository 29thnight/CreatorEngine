#include "PlayerDialogueUI.h"
#include "RectTransformComponent.h"
#include "ImageComponent.h"
#include "Camera.h"
#include "SceneManager.h"
#include "LogSystem.h"
#include "pch.h"

void PlayerDialogueUI::Start()
{
    // 컴포넌트 캐시
    m_rect = m_pOwner->GetComponent<RectTransformComponent>();
    m_image = m_pOwner->GetComponent<ImageComponent>();
    if (m_rect)
        m_rect->SetPivot({ 0.f, 0.f }); // 말풍선 꼬리 아래 가정

    // 카메라 캐시
    if (!m_camera)
    {
        auto cameraPtr = CameraManagement->GetLastCamera();
        if (cameraPtr)
        {
            m_camera = cameraPtr.get();
        }
        return;
    }
}

void PlayerDialogueUI::Update(float tick)
{
    UpdateScreenPositionAndPivot(tick);
}

void PlayerDialogueUI::UpdateScreenPositionAndPivot(float /*tick*/)
{
    if (!m_camera || !m_rect) return;

    auto targetPtr = m_target.lock();
    if (!targetPtr) return;

    // 월드→스크린
    const Mathf::Vector4 worldPos = targetPtr->Transform_().GetWorldPosition();
    const auto view = m_camera->CalculateView();
    const auto proj = m_camera->CalculateProjection();
    const auto viewProj = XMMatrixMultiply(view, proj);

    DirectX::XMVECTOR pos = XMVectorSet(worldPos.x, worldPos.y, worldPos.z, 1.0f);
    DirectX::XMVECTOR clip = XMVector4Transform(pos, viewProj);
    float w = XMVectorGetW(clip);
    if (w <= 0.0f) return; // 카메라 뒤

    float x_ndc = XMVectorGetX(clip) / w;
    float y_ndc = XMVectorGetY(clip) / w;

    const auto screenSize = m_camera->GetScreenSize();
    float screenX = (x_ndc + 1.0f) * 0.5f * screenSize.width;
    float screenY = (1.0f - y_ndc) * 0.5f * screenSize.height;

    // 좌/우 피벗 자동 전환
    float desiredPivotX = 0.0f; // 기본: 좌하단(0,0)
    if (auto cmp = m_compareTarget.lock())
    {
        const float myX = worldPos.x;
        const float cmpX = Mathf::Vector4(cmp->Transform_().GetWorldPosition()).x;
        desiredPivotX = (myX < cmpX) ? 0.0f : 1.0f; // 왼쪽이면 우하단(1,0), 오른쪽이면 좌하단(0,0)
    }

    const auto curPivot = m_rect->GetPivot();
    if (fabsf(curPivot.x - desiredPivotX) > 1e-3f || fabsf(curPivot.y - 0.0f) > 1e-3f)
        m_rect->SetPivot({ desiredPivotX, 0.0f });

    float xOffset = screenOffset.x;
    if (desiredPivotX == 0.0f) xOffset += sideOffsetPixels;  // 좌하단→오른쪽
    else                       xOffset -= sideOffsetPixels;  // 우하단→왼쪽

    m_rect->SetAnchoredPosition({ screenX + xOffset, screenY + screenOffset.y });
}

void PlayerDialogueUI::ShowTexture(int textureIndex)
{
    if (!m_image) return;
    m_image->SetTexture(textureIndex); // 인덱스 == 텍스처 ID
    m_image->SetEnabled(true);
}

void PlayerDialogueUI::Hide()
{
    if (m_image) m_image->SetEnabled(false);
}
