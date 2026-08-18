#include "UIButton.h"
#include "RHI/ScreenSizedResource.h"
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
	// S3 — UI는 Transform을 갖지 않는다. 예전에는 여기서 소유자의 트랜스폼에서
	// 월드 쿼터니언을 읽었는데, **그 값은 항상 항등이었다**: UI 오브젝트의 월드
	// 행렬은 아무도 갱신하지 않고(Scene::UpdateModelRecursive의 UI 분기가 아무
	// 일도 하지 않는다) TransformStore 슬롯의 초기값이 항등 쿼터니언이다.
	// 게다가 바로 아래 줄이 w를 1로 덮어써서 결과는 어차피 항등이었다.
	// 죽은 읽기를 지우고 뜻을 코드로 드러낸다 — UI 클릭박스는 회전하지 않는다.
	// (월드 공간 회전 UI가 필요해지면 RectTransform 쪽에 회전을 두어야 한다.)
	obBox.Orientation = Mathf::Vector4(0.f, 0.f, 0.f, 1.f);
}

bool UIButton::CheckClick(Mathf::Vector2 _mousePos)
{
	Mathf::Vector2 gameViewPos = InputManagement->m_gameViewPos;
	Mathf::Vector2 gameViewSize = InputManagement->m_gameViewSize;
	// 화면 크기 버스에서 읽는다 - UIManager의 캔버스 크기 계산과 같은 출처다.
	Mathf::Vector2 screenSize = {
		static_cast<float>(ScreenResizeBus::Get().GetWidth()),
		static_cast<float>(ScreenResizeBus::Get().GetHeight())
	};
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

void UIButton::Click()
{
	// C++ 콜백이 없어도 세운다 — C# 쪽은 ConsumeClicked 폴링으로 받는다.
	m_wasClicked = true;

	if (m_clickFunction)
	{
		m_clickFunction();
	}
}
