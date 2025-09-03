#include "UIManager.h"
#include "SceneManager.h"
#include "Scene.h"
#include "Canvas.h"
#include "ImageComponent.h"	
#include "UIButton.h"
#include "InputManager.h"
#include "TextComponent.h"
#include "RectTransformComponent.h"
#include "../RenderEngine/DeviceState.h"

std::shared_ptr<GameObject> UIManager::MakeCanvas(std::string_view name)
{
	auto newObj = SceneManagers->GetActiveScene()->CreateGameObject(name, GameObjectType::Canvas);
	newObj->AddComponent<Canvas>();

	if (auto* rect = newObj->GetComponent<RectTransformComponent>())
	{
		const std::array<float, 2> screenSize{
				static_cast<float>(DirectX11::GetWidth()),
				static_cast<float>(DirectX11::GetHeight())
		};
		rect->SetAnchoredPosition({ -screenSize[0] * 0.5f, -screenSize[1] * 0.5f });
		rect->SetSizeDelta({ screenSize[0], screenSize[1] });
		rect->UpdateLayout({ 0.0f, 0.0f, screenSize[0], screenSize[1] });
	}

	Canvases.emplace_back(newObj);
	needSort = true;
	return newObj;
}

std::shared_ptr<GameObject> UIManager::MakeImage(std::string_view name, const std::shared_ptr<Texture>& texture, GameObject* canvas, Mathf::Vector2 Pos)
{
	if (Canvases.empty())
		MakeCanvas();
	if (!canvas)
	{
		if (auto c = Canvases.front().lock())
			canvas = c.get();
		else
			return nullptr;
	}
	auto canvasCom = canvas->GetComponent<Canvas>();
	auto canvasRect = canvas->GetComponent<RectTransformComponent>();
	if(!canvasCom || !canvasRect)
	{
		std::cout << "This Obj Not Canvas" << std::endl;
		return nullptr;
	}
    auto newImage = SceneManagers->GetActiveScene()->CreateGameObject(name, GameObjectType::UI, canvas->m_index);
    if (auto* rect = newImage->GetComponent<RectTransformComponent>())
    {
		rect->SetAnchorPreset(AnchorPreset::MiddleCenter);
		rect->SetPivot({ 0.5f, 0.5f });
        rect->SetAnchoredPosition(Pos);
        rect->UpdateLayout(canvasRect->GetWorldRect());
    }
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

std::shared_ptr<GameObject> UIManager::MakeImage(std::string_view name, const std::shared_ptr<Texture>& texture, std::string_view canvasname, Mathf::Vector2 Pos)
{
	if (Canvases.empty())
		MakeCanvas();
	int canvasIndex = 0;
	for (canvasIndex = 0; canvasIndex < Canvases.size(); canvasIndex++)
	{
		if (auto c = Canvases[canvasIndex].lock())
		{
			if (c->ToString() == canvasname)
				break;
		}
	}

	GameObject* canvas = FindCanvasName(canvasname);
	if (canvas == nullptr)
	{
        auto newImage = SceneManagers->GetActiveScene()->CreateGameObject(name, GameObjectType::UI);
        if (auto* rect = newImage->GetComponent<RectTransformComponent>())
        {
            rect->SetAnchoredPosition(Pos);
            rect->UpdateLayout({ 0.0f, 0.0f, DirectX11::GetWidth(), DirectX11::GetHeight() });
        }
	}

	auto newButton0 = SceneManagers->GetActiveScene()->CreateGameObject(name, GameObjectType::UI, canvas->m_index);
	if (auto* rect = newButton0->GetComponent<RectTransformComponent>())
	{
		rect->SetAnchoredPosition(Pos);
		rect->UpdateLayout({ 0.0f, 0.0f, DirectX11::GetWidth(), DirectX11::GetHeight() });
	}
	auto newButton1 = SceneManagers->GetActiveScene()->CreateGameObject(name, GameObjectType::UI, canvas->m_index);
	if (auto* rect = newButton1->GetComponent<RectTransformComponent>())
	{
		rect->SetAnchoredPosition(Pos);
		rect->UpdateLayout({ 0.0f, 0.0f, DirectX11::GetWidth(), DirectX11::GetHeight() });
	}

	auto newText0 = SceneManagers->GetActiveScene()->CreateGameObject(name, GameObjectType::UI, canvas->m_index);
	if (auto* rect = newText0->GetComponent<RectTransformComponent>())
	{
		rect->SetAnchoredPosition(Pos);
		rect->UpdateLayout({ 0.0f, 0.0f, DirectX11::GetWidth(), DirectX11::GetHeight() });
	}

	auto newText1 = SceneManagers->GetActiveScene()->CreateGameObject(name, GameObjectType::UI, canvas->m_index);
	if (auto* rect = newText1->GetComponent<RectTransformComponent>())
	{
		rect->SetAnchoredPosition(Pos);
		rect->UpdateLayout({ 0.0f, 0.0f, DirectX11::GetWidth(), DirectX11::GetHeight() });
	}

       
	if (Canvases.empty())
		MakeCanvas();
	if (!canvas)
	{
		if (auto c = Canvases.front().lock())
			canvas = c.get();
		else
			return nullptr;
	}
	auto canvasCom = canvas->GetComponent<Canvas>();
	if (!canvasCom)
	{
		std::cout << "This Obj Not Canvas" << std::endl;
		return nullptr;
	}
	auto newButton = SceneManagers->GetActiveScene()->CreateGameObject(name, GameObjectType::UI, canvas->m_index);
	newButton->m_transform.SetPosition({ Pos.x, Pos.y, 0 }); // 960 540이 기본값 화면중앙
	newButton->AddComponent<ImageComponent>()->Load(texture);
	auto component = newButton->AddComponent<UIButton>();
	//component->SetClickFunction(clickfun);

	canvasCom->AddUIObject(newButton.get());

	return newButton;
}



std::shared_ptr<GameObject> UIManager::MakeButton(std::string_view name, const std::shared_ptr<Texture>& texture, std::function<void()> clickfun, std::string_view canvasname,  Mathf::Vector2 Pos)
{
	if (Canvases.empty())
		MakeCanvas();
	int canvasIndex = 0;
	for (canvasIndex = 0; canvasIndex < Canvases.size(); canvasIndex++)
	{
		if (auto c = Canvases[canvasIndex].lock())
		{
			if (c->ToString() == canvasname)
				break;
		}
	}
	GameObject* canvas = FindCanvasName(canvasname);
	if (canvas == nullptr)
	{
		std::cout << "해당 이름의 캔버스가 없습니다." << std::endl;
		return nullptr;
	}
	auto newButton = SceneManagers->GetActiveScene()->CreateGameObject(name, GameObjectType::UI, canvas->m_index);
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
	{
		if (auto c = Canvases.front().lock())
			canvas = c.get();
		else
			return nullptr;
	}
	auto canvasCom = canvas->GetComponent<Canvas>();
	auto newText = SceneManagers->GetActiveScene()->CreateGameObject(name, GameObjectType::UI, canvas->m_index);
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
		if (auto c = Canvases[canvasIndex].lock())
		{
			if (c->ToString() == canvasname)
				break;
		}
	}
	GameObject* canvas = FindCanvasName(canvasname);
	if (canvas == nullptr)
	{
		std::cout << "해당 이름의 캔버스가 없습니다." << std::endl;
		return nullptr;
	}
	auto newText = SceneManagers->GetActiveScene()->CreateGameObject(name, GameObjectType::UI, canvas->m_index);
	newText->m_transform.SetPosition({ Pos.x, Pos.y, 0 }); // 960 540이 기본값 화면중앙
	newText->AddComponent<TextComponent>()->LoadFont(Sfont);
	canvas->GetComponent<Canvas>()->AddUIObject(newText.get());

	return newText;
}

void UIManager::DeleteCanvas(std::string canvasName)
{
	std::erase_if(Canvases, [&](const std::weak_ptr<GameObject>& canvas) 
	{
		auto c = canvas.lock();
		return !c || c->ToString() == canvasName;
	});
}

void UIManager::CheckInput()
{
	auto curCanvasObj = CurCanvas.lock();
	if (!curCanvasObj) return;
	Canvas* curCanvas = curCanvasObj->GetComponent<Canvas>();
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
	for (auto it = Canvases.begin(); it != Canvases.end();)
	{
		if (auto canvasObj = it->lock())
		{
			if (canvasObj->ToString() == name)
				return canvasObj.get();
			++it;
		}
		else
		{
			it = Canvases.erase(it);
		}
	}
	return nullptr;
}

void UIManager::Update()
{
	SortCanvas();
	
	for (int i = static_cast<int>(Canvases.size()) - 1; i >= 0; i--)
	{
		auto canvasPtr = Canvases[i].lock();
		if (!canvasPtr)
		{
			Canvases.erase(Canvases.begin() + i);
			continue;
		}
		if (!canvasPtr->GetComponent<Canvas>()->IsEnabled()) continue;
		CurCanvas = canvasPtr;
		break;
	}

	CheckInput();
}

void UIManager::SortCanvas()
{
	if (needSort == false)
	{
		return;
	}
	else
	{
		std::erase_if(Canvases, [](const std::weak_ptr<GameObject>& canvas) {
			return canvas.expired() || !canvas.lock()->GetComponent<Canvas>()->IsEnabled();
		});

		std::ranges::sort(Canvases, [](const std::weak_ptr<GameObject>& a, const std::weak_ptr<GameObject>& b) {
			auto aCanvas = a.lock();
			auto bCanvas = b.lock();
			if (aCanvas && bCanvas)
				return aCanvas->GetComponent<Canvas>()->GetCanvasOrder() < bCanvas->GetComponent<Canvas>()->GetCanvasOrder();
			return false; // 둘 중 하나가 nullptr인 경우 false 반환
		});

	}
	needSort = false;
}
