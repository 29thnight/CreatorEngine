#include "UIButton.h"
#include "../RenderEngine/DeviceState.h"
#include "InputManager.h"
#include "ImageComponent.h"
#include "RectTransformComponent.h"

void UIButton::Update(float deltaSecond)
{
	UpdateCollider();
}

void UIButton::UpdateCollider()
{
	
        if (auto* rect = m_pOwner->GetComponent<RectTransformComponent>())
        {
                const auto& worldRect = rect->GetWorldRect();
                obBox.Center = { worldRect.x + worldRect.width * 0.5f,
                                 worldRect.y + worldRect.height * 0.5f,
                                 0.0f };
                obBox.Extents.x = worldRect.width * 0.5f;
                obBox.Extents.y = worldRect.height * 0.5f;
        }
        auto quater = m_pOwner->m_transform.GetWorldQuaternion();
        obBox.Orientation = quater;
}

bool UIButton::CheckClick(Mathf::Vector2 _mousePos)
{
	Mathf::Vector2 gameViewPos = InputManagement->m_gameViewPos;
	Mathf::Vector2 gameViewSize = InputManagement->m_gameViewSize;
	Mathf::Vector2 screenSize = { DirectX11::GetWidth(), DirectX11::GetHeight() };
	float localX = (_mousePos.x - gameViewPos.x) * (screenSize.x / gameViewSize.x);
	float localY = (_mousePos.y - gameViewPos.y) * (screenSize.y / gameViewSize.y);

	XMVECTOR pointWS = XMVectorSet(localX, localY, 0.0f, 0.0f);

	XMVECTOR center = XMVectorSet(obBox.Center.x,
		obBox.Center.y,
		obBox.Center.z,
		0.0f);
	XMVECTOR extents = XMVectorSet(obBox.Extents.x,
		obBox.Extents.y,
		obBox.Extents.z,
		0.0f);
	XMVECTOR orientation = XMVectorSet(obBox.Orientation.x,
		obBox.Orientation.y,
		obBox.Orientation.z,
		obBox.Orientation.w);

	XMVECTOR dir = XMVectorSubtract(pointWS, center);
	XMVECTOR dirLocal = XMVector3Rotate(dir, XMQuaternionConjugate(orientation));
	XMFLOAT3 localF;
	XMStoreFloat3(&localF, dirLocal);

	if (fabsf(localF.x) <= obBox.Extents.x &&
		fabsf(localF.y) <= obBox.Extents.y)
	{
		isClick = true;
		return true;
	}
	isClick = false;
	return false;
}