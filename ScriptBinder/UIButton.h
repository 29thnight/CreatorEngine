#pragma once
#include "../Utility_Framework/Core.Minimal.h"
#include "Component.h"
#include "IRegistableEvent.h"
#include "UIManager.h"
#include "UIComponent.h"
#include "UIButton.generated.h"

//UI 처리용
enum class UIColliderType : uint8_t
{
	Box,
	Circle,
	Capsule,
};
AUTO_REGISTER_ENUM(UIColliderType);

class UIButton : public UIComponent
{
public:
   ReflectUIButton
    [[Serializable(Inheritance:UIComponent)]]
	GENERATED_BODY(UIButton)
	
	void Update(float deltaSecond) override;

	void SetClickFunction(std::function<void()> func)
	{
		m_clickFunction = func;
	}
	void UpdateCollider();
	bool CheckClick(Mathf::Vector2 _mousePos);
	void SetFunction(std::string& funName,float key,std::function<void()> func) { m_clickFunction = func;}
    [[Method]]
	void Click();

	// 클릭 판정에 쓰이는 상자. 렌더 좌표와 입력 좌표가 같은 사각형을 가리키는지
	// 검증하기 위해 노출한다 — 둘이 어긋나면 "보이는 곳과 눌리는 곳"이 달라진다(PHASE 7-7).
	const DirectX::BoundingOrientedBox& GetCollider() const { return obBox; }

	// C# 폴링용. Click()이 세우고, 스크립트가 틱에서 읽으면 내려간다.
	// 콜백 델리게이트를 경계 너머로 넘기는 대신 "틱당 1회" 규약대로 폴링한다 —
	// 클릭은 프레임당 최대 1회라 래치 하나로 유실 없이 전달된다.
	bool ConsumeClicked()
	{
		const bool wasClicked = m_wasClicked;
		m_wasClicked = false;
		return wasClicked;
	}

private:
	DirectX::BoundingOrientedBox obBox;
	std::function<void()> m_clickFunction;
	bool m_wasClicked = false;
	UIColliderType type = UIColliderType::Box;

public:
	bool isClick = false;
};

