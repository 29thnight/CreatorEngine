#include "PlayerObserver.h"
#include "RectTransformComponent.h"
#include "SceneManager.h"
#include "Camera.h"
#include "ImageComponent.h"
#include "Entity.h"
#include "pch.h"
void PlayerObserver::Start()
{
	if (!m_image)
		m_image = m_pOwner->GetComponent<ImageComponent>();
	if (m_image)
		m_image->color.w = 1.0f;

	m_elapsedTime = 0.0f;
}

void PlayerObserver::Update(float tick)
{
    if (m_target.expired() || nullptr == m_rect || nullptr == m_image)
    {
        GetOwner()->Destroy();
        return;
    }

    m_elapsedTime += tick;

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

    // --- 기존 화면 좌표 변환 로직 유지 ---
    Mathf::Vector3 worldPos = target_ptr->m_transform.GetWorldPosition();
    auto view = m_camera->CalculateView();
    auto proj = m_camera->CalculateProjection();
    auto viewProj = XMMatrixMultiply(view, proj);

    DirectX::XMVECTOR pos = XMVectorSet(worldPos.x, worldPos.y, worldPos.z, 1.0f);
    DirectX::XMVECTOR clip = XMVector4Transform(pos, viewProj);
    float w = XMVectorGetW(clip);
    if (w <= 0.0f)
    {
        return;
    }

    float x_ndc = XMVectorGetX(clip) / w;
    float y_ndc = XMVectorGetY(clip) / w;

    auto screenSize = m_camera->GetScreenSize();
    float screenX = (x_ndc + 1.0f) * 0.5f * screenSize.width;
    float screenY = (1.0f - y_ndc) * 0.5f * screenSize.height;

    m_rect->SetAnchoredPosition({ screenX + screenOffset.x, screenY + screenOffset.y });

    // --- 2초 대기 후 알파 LERP (1 -> 0) ---
    if (m_elapsedTime <= WaitBeforeFade)
    {
        // 대기 시간엔 항상 완전 불투명
        m_image->color.w = 1.f;
        return;
    }

    // 진행도: (경과시간-대기)/지속, 여기선 m_lerpSpeed를 "초당 진행률"로 사용
    // ex) m_lerpSpeed=2.0 => 0.5초에 1.0까지 진행
    float t = (m_elapsedTime - WaitBeforeFade) * m_lerpSpeed;
    t = std::clamp(t, 0.0f, 1.0f);

    // 부드럽게 하고 싶으면 주석 해제해서 SmoothStep 적용
    // t = t * t * (3.0f - 2.0f * t);

    float alpha = 1.0f - t; // 1 -> 0
	m_image->color.w = alpha;
}

void PlayerObserver::Init()
{
	if (auto targetPtr = m_target.lock())
	{
		m_rect = m_pOwner->GetComponent<RectTransformComponent>();
		m_image = m_pOwner->GetComponent<ImageComponent>();
		m_image->SetTexture(m_playerIndex);

		auto canvasObj = GameObject::Find("Canvas");
        if (canvasObj)
        {
			canvasObj->AddChild(m_pOwner);
		}
	}
}

