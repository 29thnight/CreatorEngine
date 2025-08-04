#include "UIButton.h"
#include "../RenderEngine/DeviceState.h"
#include "InputManager.h"
#include "ImageComponent.h"

void UIButton::Update(float deltaSecond)
{
	UpdateCollider();
}

void UIButton::UpdateCollider()
{
	
	Transform transform = m_pOwner->m_transform;
	Mathf::Vector4 pos = transform.position;
	Mathf::Vector4 scale = transform.scale;
	ImageComponent* Image = m_pOwner->GetComponent<ImageComponent>();
	obBox.Center = Mathf::Vector3{ pos.x, pos.y, 0 };
	if (Image)
	{
		obBox.Extents.x = Image->uiinfo.size.x / 2 * scale.x;
		obBox.Extents.y = Image->uiinfo.size.y / 2 * scale.y;
	}
	Mathf::Vector4 quater = transform.GetWorldQuaternion();
	obBox.Orientation = quater;
	//obBox.Orientation = { XMVectorGetX(quater), XMVectorGetY(quater),XMVectorGetZ(quater) ,XMVectorGetW(quater) };
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