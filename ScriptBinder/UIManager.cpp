#include "UIManager.h"
#include "SceneManager.h"
#include "Scene.h"
#include "Canvas.h"
#include "UIComponent.h"	
#include "UICollider.h"
#include "UIButton.h"
#include "../InputManager.h"
std::shared_ptr<GameObject> UIManager::MakeCanvas(const std::string_view& name)
{

	auto  newObj = SceneManagers->GetActiveScene()->CreateGameObject(name, GameObject::Type::Empty);
	newObj->AddComponent<Canvas>();
	Canvases.push_back(newObj.get());

	return newObj;
}

std::shared_ptr<GameObject> UIManager::MakeImage(const std::string_view& name,Texture* texture,Mathf::Vector2 Pos, GameObject* canvas)
{
	if (Canvases.empty())
	{
		MakeCanvas("Main");
	}
	int canvasIndex =0;
	if(canvas)
	{
		for (canvasIndex = 0; canvasIndex < Canvases.size(); canvasIndex++)
		{
			if (Canvases[canvasIndex] == canvas)
				break;
		}
	}
	
	auto newImage = SceneManagers->GetActiveScene()->CreateGameObject(name, GameObject::Type::Mesh, Canvases[canvasIndex]->m_index);
	newImage->m_transform.SetPosition({ Pos.x, Pos.y, 0 }); // 960 540이 기본값 화면중앙
	newImage->AddComponent<UIComponent>()->Load(texture);
	Canvases[canvasIndex]->GetComponent<Canvas>()->AddUIObject(newImage.get());

	return newImage;
}

std::shared_ptr<GameObject> UIManager::MakeButton(const std::string_view& name, Texture* texture, std::function<void()> clickfun, Mathf::Vector2 Pos, GameObject* canvas)
{

	if (Canvases.empty())
	{
		MakeCanvas();
	}
	int canvasIndex = 0;
	if (canvas)
	{
		for (canvasIndex = 0; canvasIndex < Canvases.size(); canvasIndex++)
		{
			if (Canvases[canvasIndex] == canvas)
				break;
		}
	}
	auto newButton = SceneManagers->GetActiveScene()->CreateGameObject(name, GameObject::Type::Mesh, Canvases[canvasIndex]->m_index);
	newButton->m_transform.SetPosition({ Pos.x, Pos.y, 0 }); // 960 540이 기본값 화면중앙
	newButton->AddComponent<UIComponent>()->Load(texture);
	newButton->AddComponent<UICollider>();
	newButton->GetComponent<UICollider>()->SetCollider();
	newButton->AddComponent<UIButton>(clickfun);

	Canvases[canvasIndex]->GetComponent<Canvas>()->AddUIObject(newButton.get());

	return newButton;
}

void UIManager::CheckInput()
{
	
	if (InputManagement->IsMouseButtonReleased(MouseKey::LEFT))
	{
		for (auto& Canvases : Canvases)
		{
			Canvas* canvas = Canvases->GetComponent<Canvas>();
			if (false == canvas->IsEnabled()) continue;
			for (auto& uiObj : canvas->UIObjs)
			{
				UIComponent* UI = uiObj->GetComponent<UIComponent>();
				if (false == UI->IsEnabled()) continue;
				UICollider* collider = uiObj->GetComponent<UICollider>();
				if (collider == nullptr) continue;
				if (collider->CheckClick(InputManagement->GetMousePos()) == false) continue;
				UIButton* btn = uiObj->GetComponent<UIButton>();
				if (btn == nullptr) continue;
					btn->Click();
					break;
			}
		}
	}

	Mathf::Vector2 stickL = InputManagement->GetControllerThumbL(0);
	if (stickL.x > 0.5)
	{
		SelectUI = SelectUI->GetComponent<UIComponent>()->GetNextNavi(Direction::Right);
	}
	if (stickL.x < -0.5)
	{
		
		SelectUI = SelectUI->GetComponent<UIComponent>()->GetNextNavi(Direction::Left);
	}

	if (InputManagement->IsControllerButtonReleased(0, ControllerButton::A))
	{
		if (SelectUI == nullptr) return;
		SelectUI->GetComponent<UIButton>()->Click();
	}

	//테스트용
	if (InputManagement->IsControllerButtonReleased(0, ControllerButton::START_BUTTON))
	{
		std::cout << "캔버스 켰음" << std::endl;
		for (auto& Canvases : Canvases)
		{
			if (Canvases->ToString() == "setting")
			{
				Canvas* canvas = Canvases->GetComponent<Canvas>();
				canvas->SetEnabled(true);
			}
		}
	}

	if (InputManagement->IsControllerButtonReleased(0, ControllerButton::BACK_BUTTON))
	{
		for (auto& Canvases : Canvases)
		{
			if (Canvases->ToString() == "setting")
			{
				Canvas* canvas = Canvases->GetComponent<Canvas>();
				canvas->SetEnabled(false);
			}
		}
	}
	
}
