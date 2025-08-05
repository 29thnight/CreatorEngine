#include "UIManager.h"
#include "SceneManager.h"
#include "Scene.h"
#include "Canvas.h"
#include "ImageComponent.h"	
#include "UIButton.h"
#include "InputManager.h"
#include "TextComponent.h"

std::shared_ptr<GameObject> UIManager::MakeCanvas(std::string_view name)
{
	auto  newObj = SceneManagers->GetActiveScene()->CreateGameObject(name, GameObjectType::Empty);
	newObj->AddComponent<Canvas>();
	Canvases.push_back(newObj.get());
	needSort = true;
	return newObj;
}

std::shared_ptr<GameObject> UIManager::MakeImage(std::string_view name,Texture* texture, GameObject* canvas,Mathf::Vector2 Pos)
{
	if (Canvases.empty())
		MakeCanvas();
	if (!canvas)
		canvas = Canvases[0];
	auto canvasCom = canvas->GetComponent<Canvas>();
	if(!canvasCom)
	{
		std::cout << "This Obj Not Canvas" << std::endl;
		return nullptr;
	}
	auto newImage = SceneManagers->GetActiveScene()->CreateGameObject(name, GameObjectType::Mesh, canvas->m_index);
	newImage->m_transform.SetPosition({ Pos.x, Pos.y, 0 }); // 960 540이 기본값 화면중앙
	if (texture == nullptr)
	{
		newImage->AddComponent<ImageComponent>();
	}
	else
	{
		newImage->AddComponent<ImageComponent>()->Load(texture);
	}

	canvasCom->AddUIObject(newImage.get());
	

	return newImage;
}

std::shared_ptr<GameObject> UIManager::MakeImage(std::string_view name, Texture* texture, std::string_view canvasname, Mathf::Vector2 Pos)
{
	if (Canvases.empty())
		MakeCanvas();
	int canvasIndex = 0;
	for (canvasIndex = 0; canvasIndex < Canvases.size(); canvasIndex++)
	{
		if (Canvases[canvasIndex]->ToString() == canvasname)
			break;
	}

	GameObject* canvas = FindCanvasName(canvasname);
	if (canvas == nullptr)
	{
		std::cout << "해당 이름의 캔버스가 없습니다." << std::endl;
		return nullptr;
	}
	auto newImage = SceneManagers->GetActiveScene()->CreateGameObject(name, GameObjectType::Mesh, canvas->m_index);
	newImage->m_transform.SetPosition({ Pos.x, Pos.y, 0 }); // 960 540이 기본값 화면중앙
	newImage->AddComponent<ImageComponent>()->Load(texture);
	canvas->GetComponent<Canvas>()->AddUIObject(newImage.get());

	return newImage;
}


std::shared_ptr<GameObject> UIManager::MakeButton(std::string_view name, Texture* texture, std::function<void()> clickfun, Mathf::Vector2 Pos , GameObject* canvas)
{
	if (Canvases.empty())
		MakeCanvas();
	if (!canvas)
		canvas = Canvases[0];
	auto canvasCom = canvas->GetComponent<Canvas>();
	if (!canvasCom)
	{
		std::cout << "This Obj Not Canvas" << std::endl;
		return nullptr;
	}
	auto newButton = SceneManagers->GetActiveScene()->CreateGameObject(name, GameObjectType::Mesh, canvas->m_index);
	newButton->m_transform.SetPosition({ Pos.x, Pos.y, 0 }); // 960 540이 기본값 화면중앙
	newButton->AddComponent<ImageComponent>()->Load(texture);
	auto component = newButton->AddComponent<UIButton>();
	component->SetClickFunction(clickfun);

	canvasCom->AddUIObject(newButton.get());

	return newButton;
}



std::shared_ptr<GameObject> UIManager::MakeButton(std::string_view name, Texture* texture, std::function<void()> clickfun, std::string_view canvasname,  Mathf::Vector2 Pos)
{
	if (Canvases.empty())
		MakeCanvas();
	int canvasIndex = 0;
	for (canvasIndex = 0; canvasIndex < Canvases.size(); canvasIndex++)
	{
		if (Canvases[canvasIndex]->ToString() == canvasname)
			break;
	}
	GameObject* canvas = FindCanvasName(canvasname);
	if (canvas == nullptr)
	{
		std::cout << "해당 이름의 캔버스가 없습니다." << std::endl;
		return nullptr;
	}
	auto newButton = SceneManagers->GetActiveScene()->CreateGameObject(name, GameObjectType::Mesh, canvas->m_index);
	newButton->m_transform.SetPosition({ Pos.x, Pos.y, 0 }); // 960 540이 기본값 화면중앙
	newButton->AddComponent<ImageComponent>()->Load(texture);
	auto component = newButton->AddComponent<UIButton>();
	component->SetClickFunction(clickfun);


	canvas->GetComponent<Canvas>()->AddUIObject(newButton.get());

	return newButton;
}

std::shared_ptr<GameObject> UIManager::MakeText(std::string_view name, SpriteFont* Sfont, GameObject* canvas, Mathf::Vector2 Pos)
{
	if (Canvases.empty())
		MakeCanvas();
	if (!canvas)
		canvas = Canvases[0];
	auto canvasCom = canvas->GetComponent<Canvas>();
	if (!canvasCom)
	{
		std::cout << "This Obj Not Canvas" << std::endl;
		return nullptr;
	}
	auto newText = SceneManagers->GetActiveScene()->CreateGameObject(name, GameObjectType::TypeMax, canvas->m_index);
	newText->m_transform.SetPosition({ Pos.x, Pos.y, 0 }); // 960 540이 기본값 화면중앙
	newText->AddComponent<TextComponent>()->LoadFont(Sfont);
	canvasCom->AddUIObject(newText.get());

	return newText;
}

std::shared_ptr<GameObject> UIManager::MakeText(std::string_view name, SpriteFont* Sfont, std::string_view canvasname, Mathf::Vector2 Pos)
{
	if (Canvases.empty())
		MakeCanvas();
	int canvasIndex = 0;
	for (canvasIndex = 0; canvasIndex < Canvases.size(); canvasIndex++)
	{
		if (Canvases[canvasIndex]->ToString() == canvasname)
			break;
	}
	GameObject* canvas = FindCanvasName(canvasname);
	if (canvas == nullptr)
	{
		std::cout << "해당 이름의 캔버스가 없습니다." << std::endl;
		return nullptr;
	}
	auto newText = SceneManagers->GetActiveScene()->CreateGameObject(name, GameObjectType::Empty, canvas->m_index);
	newText->m_transform.SetPosition({ Pos.x, Pos.y, 0 }); // 960 540이 기본값 화면중앙
	newText->AddComponent<TextComponent>()->LoadFont(Sfont);
	canvas->GetComponent<Canvas>()->AddUIObject(newText.get());

	return newText;
}

void UIManager::DeleteCanvas(std::string canvasName)
{
	auto it = std::find_if(Canvases.begin(), Canvases.end(),
		[&](const GameObject* canvas)
		{
			return canvas->ToString() == canvasName;
		});

	Canvases.erase(it, Canvases.end());

}

void UIManager::CheckInput()
{
	if (CurCanvas == nullptr) return;
	Canvas* curCanvas = CurCanvas->GetComponent<Canvas>();
	if (InputManagement->IsMouseButtonReleased(MouseKey::LEFT))
	{
		for (auto& uiObj : curCanvas->UIObjs)
		{
			
			UIComponent* UI = uiObj->GetComponent<UIComponent>();
			if (UI && false == UI->IsEnabled()) continue;
			UIButton* btn = uiObj->GetComponent<UIButton>();
			if (btn == nullptr || btn->CheckClick(InputManagement->GetMousePos()) == false) continue;
			btn->Click();
			break;
		}
	}


	//0을 1p,2p로 바꾸거나 둘다따로 주게 수정필요, 이동마다 대기시간 딜레이 주기 한번에 여러개 못넘어가게 *****
	Mathf::Vector2 stickL = InputManagement->GetControllerThumbL(0);
	if (stickL.x > 0.5)
	{
		curCanvas->SelectUI = curCanvas->SelectUI->GetComponent<ImageComponent>()->GetNextNavi(Direction::Right);
	}
	if (stickL.x < -0.5)
	{
		curCanvas->SelectUI = curCanvas->SelectUI->GetComponent<ImageComponent>()->GetNextNavi(Direction::Left);
	}

	if (InputManagement->IsControllerButtonReleased(0, ControllerButton::A))
	{
		if (curCanvas->SelectUI == nullptr) return;
		curCanvas->SelectUI->GetComponent<UIButton>()->Click();
	}	
}

GameObject* UIManager::FindCanvasName(std::string_view name)
{
	for (auto& canvasObj : Canvases)
	{
		if (canvasObj && canvasObj->ToString() == name)
			return canvasObj;
	}
	return nullptr;
}

void UIManager::Update()
{
	SortCanvas();
	
	for (int i = Canvases.size() -1; i >= 0; i--)
	{
		if (!Canvases[i]->GetComponent<Canvas>()->IsEnabled()) continue;
		CurCanvas = Canvases[i];
		break;
	}
	CheckInput();
}

void UIManager::SortCanvas()
{
	if(needSort == false)return;
	else
	{
		std::sort(Canvases.begin(), Canvases.end(), [](GameObject* a, GameObject* b) {
			auto aCanvas = a->GetComponent<Canvas>();
			auto bCanvas = b->GetComponent<Canvas>();

			if (aCanvas && bCanvas)
				return aCanvas->CanvasOrder < bCanvas->CanvasOrder;
			return false; // 정렬 기준 없음
			});
	}
	needSort = false;
}
