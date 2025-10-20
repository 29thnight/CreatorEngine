#include "UIManager.h"
#include "SceneManager.h"
#include "Scene.h"
#include "Canvas.h"
#include "ImageComponent.h"
#include "UIButton.h"
#include "InputManager.h"
#include "TextComponent.h"
#include "RectTransformComponent.h"
#include "SpriteSheetComponent.h"
#include "../RenderEngine/DeviceState.h"
#include <algorithm>

std::shared_ptr<GameObject> UIManager::MakeCanvas(std::string_view name)
{
	if(auto existingCanvas = FindCanvasName(name); existingCanvas )
	{
		std::cout << "Canvas with name '" << name << "' already exists." << std::endl;
		return nullptr;
	}

	auto curScene = SceneManagers->GetActiveScene();
	if (curScene == nullptr) return nullptr;

	auto& CanvasMap = curScene->GetCanvasMap();
	auto& Canvases = curScene->GetCanvases();

	auto newObj = SceneManagers->GetActiveScene()->CreateGameObject(name, GameObjectType::Canvas);
	auto can = newObj->AddComponent<Canvas>();

    if (auto* rect = newObj->GetComponent<RectTransformComponent>())
    {
        const std::array<float, 2> screenSize{
            static_cast<float>(DirectX11::GetWidth()),
            static_cast<float>(DirectX11::GetHeight())
        };
        rect->SetAnchoredPosition({ 0.0f, 0.0f });
        rect->SetSizeDelta({ screenSize[0], screenSize[1] });
        rect->UpdateLayout({ 0.0f, 0.0f, screenSize[0], screenSize[1] });
    }

	Canvases.emplace_back(newObj);
	CanvasMap[name.data()] = newObj;

	needSort = true;
	return newObj;
}

void UIManager::AddCanvas(std::shared_ptr<GameObject> canvas)
{
	if (!canvas)
	{
		std::cout << "Canvas is nullptr" << std::endl;
		return;
	}
	auto canvasCom = canvas->GetComponent<Canvas>();
	if (!canvasCom)
	{
		std::cout << "This Obj Not Canvas" << std::endl;
		return;
	}

	auto curScene = canvas->GetScene();
	if (curScene == nullptr) return;

	std::string canvasName = canvas->ToString();
	auto& CanvasMap = curScene->GetCanvasMap();
	auto& Canvases = curScene->GetCanvases();

	if (CanvasMap.find(canvasName) != CanvasMap.end())
	{
		if (auto existingCanvas = CanvasMap[canvasName].lock())
		{
			std::cout << "Canvas with name '" << canvasName << "' already exists." << std::endl;
			return;
		}
		else
		{
			CanvasMap.erase(canvasName);
			std::erase_if(Canvases, [&](const std::weak_ptr<GameObject>& weakPtr) {
				if (auto sharedPtr = weakPtr.lock())
				{
					return sharedPtr->ToString() == canvasName;
				}
				return true; // Remove expired weak_ptr
			});
		}
	}

	Canvases.emplace_back(canvas);
	CanvasMap[canvas->ToString()] = canvas;
	needSort = true;
}

std::shared_ptr<GameObject> UIManager::MakeImage(std::string_view name, const std::shared_ptr<Texture>& texture, GameObject* canvas, Mathf::Vector2 Pos)
{
	auto curScene = SceneManagers->GetActiveScene();
	if (curScene == nullptr) return nullptr;

	if (curScene->GetCanvases().empty())
		MakeCanvas();

	if (!canvas)
	{
		if (auto c = curScene->GetCanvases().front().lock())
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
	ImageComponent* imageComp{};
	if (texture == nullptr)
	{
		imageComp = newImage->AddComponent<ImageComponent>();
	}
	else
	{
		imageComp = newImage->AddComponent<ImageComponent>();
		imageComp->Load(texture);
	}

	canvasCom->AddUIObject(newImage);
	imageComp->isDeserialized = true;

	return newImage;
}

std::shared_ptr<GameObject> UIManager::MakeImage(std::string_view name, const std::shared_ptr<Texture>& texture, std::string_view canvasname, Mathf::Vector2 Pos)
{
	auto curScene = SceneManagers->GetActiveScene();
	if (curScene == nullptr) return nullptr;

	if (curScene->GetCanvases().empty())
            MakeCanvas();

    GameObject* canvas = FindCanvasName(canvasname);
    if (canvas == nullptr)
    {
        std::cout << "해당 이름의 캔버스가 없습니다." << std::endl;
        return nullptr;
    }

    return MakeImage(name, texture, canvas, Pos);
}

std::shared_ptr<GameObject> UIManager::MakeButton(std::string_view name, const std::shared_ptr<Texture>& texture, std::function<void()> clickfun, Mathf::Vector2 Pos, GameObject* canvas)
{
	auto curScene = SceneManagers->GetActiveScene();
	if (curScene == nullptr) return nullptr;

	auto& Canvases = curScene->GetCanvases();

	if (Canvases.empty())
	{
		MakeCanvas();
	}

    if (!canvas)
    {
        if (auto c = Canvases.front().lock())
            canvas = c.get();
        else
            return nullptr;
    }

    auto canvasCom = canvas->GetComponent<Canvas>();
    auto canvasRect = canvas->GetComponent<RectTransformComponent>();
    if (!canvasCom || !canvasRect)
    {
        std::cout << "This Obj Not Canvas" << std::endl;
        return nullptr;
    }

    auto newButton = SceneManagers->GetActiveScene()->CreateGameObject(name, GameObjectType::UI, canvas->m_index);
    if (auto* rect = newButton->GetComponent<RectTransformComponent>())
    {
        rect->SetAnchorPreset(AnchorPreset::MiddleCenter);
        rect->SetPivot({ 0.5f, 0.5f });
        rect->SetAnchoredPosition(Pos);
        rect->UpdateLayout(canvasRect->GetWorldRect());
    }
	ImageComponent* imageComp{};
    if (texture == nullptr)
    {
		imageComp = newButton->AddComponent<ImageComponent>();
    }
    else
    {
		imageComp = newButton->AddComponent<ImageComponent>();
		imageComp->Load(texture);
    }

	imageComp->isDeserialized = true;

    auto component = newButton->AddComponent<UIButton>();
    component->SetClickFunction(clickfun);
	component->isDeserialized = true;

    canvasCom->AddUIObject(newButton);

    return newButton;
}

std::shared_ptr<GameObject> UIManager::MakeButton(std::string_view name, const std::shared_ptr<Texture>& texture, std::function<void()> clickfun, std::string_view canvasname,  Mathf::Vector2 Pos)
{
	auto curScene = SceneManagers->GetActiveScene();
	if (curScene == nullptr) return nullptr;

	if (curScene->GetCanvases().empty())
         MakeCanvas();

    GameObject* canvas = FindCanvasName(canvasname);
    if (canvas == nullptr)
    {
        std::cout << "해당 이름의 캔버스가 없습니다." << std::endl;
        return nullptr;
    }

    return MakeButton(name, texture, clickfun, Pos, canvas);
}

std::shared_ptr<GameObject> UIManager::MakeText(std::string_view name, file::path FontName, GameObject* canvas, Mathf::Vector2 Pos)
{
	auto curScene = SceneManagers->GetActiveScene();
	if (curScene == nullptr) return nullptr;

	auto& Canvases = curScene->GetCanvases();

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
	if (!canvasCom || !canvasRect)
	{
		std::cout << "This Obj Not Canvas" << std::endl;
		return nullptr;
	}

	auto newText = SceneManagers->GetActiveScene()->CreateGameObject(name, GameObjectType::UI, canvas->m_index);
	if (auto* rect = newText->GetComponent<RectTransformComponent>())
	{
		rect->SetAnchorPreset(AnchorPreset::MiddleCenter);
		rect->SetPivot({ 0.5f, 0.5f });
		rect->SetAnchoredPosition(Pos);
		rect->UpdateLayout(canvasRect->GetWorldRect());
	}
	TextComponent* textComp = newText->AddComponent<TextComponent>();
	textComp->SetFont(FontName);
	textComp->isDeserialized = true;
	canvasCom->AddUIObject(newText);

	return newText;
}

std::shared_ptr<GameObject> UIManager::MakeText(std::string_view name, file::path FontName, std::string_view canvasname, Mathf::Vector2 Pos)
{
	auto curScene = SceneManagers->GetActiveScene();
	if (curScene == nullptr) return nullptr;

	if (curScene->GetCanvases().empty())
            MakeCanvas();
    GameObject* canvas = FindCanvasName(canvasname);
    if (canvas == nullptr)
    {
        std::cout << "해당 이름의 캔버스가 없습니다." << std::endl;
        return nullptr;
    }
    return MakeText(name, FontName, canvas, Pos);
}

std::shared_ptr<GameObject> UIManager::MakeSpriteSheet(std::string_view name, const file::path& spriteSheetPath, GameObject* canvas, Mathf::Vector2 Pos)
{
	auto curScene = SceneManagers->GetActiveScene();
	if (curScene == nullptr) return nullptr;

	if (curScene->GetCanvases().empty())
        MakeCanvas();
    if (!canvas)
    {
        if (auto c = curScene->GetCanvases().front().lock())
            canvas = c.get();
        else
            return nullptr;
    }
    auto canvasCom = canvas->GetComponent<Canvas>();
    auto canvasRect = canvas->GetComponent<RectTransformComponent>();
    if (!canvasCom || !canvasRect)
    {
        std::cout << "This Obj Not Canvas" << std::endl;
        return nullptr;
    }

    auto newSpriteSheet = SceneManagers->GetActiveScene()->CreateGameObject(name, GameObjectType::UI, canvas->m_index);
    if (auto* rect = newSpriteSheet->GetComponent<RectTransformComponent>())
    {
        rect->SetAnchorPreset(AnchorPreset::MiddleCenter);
        rect->SetPivot({ 0.5f, 0.5f });
        rect->SetAnchoredPosition(Pos);
        rect->UpdateLayout(canvasRect->GetWorldRect());
    }
	SpriteSheetComponent* spriteSheetCom = newSpriteSheet->AddComponent<SpriteSheetComponent>();
	spriteSheetCom->LoadSpriteSheet(spriteSheetPath);
	spriteSheetCom->isDeserialized = true;
    canvasCom->AddUIObject(newSpriteSheet);
    return newSpriteSheet;
}

std::shared_ptr<GameObject> UIManager::MakeSpriteSheet(std::string_view name, const file::path& spriteSheetPath, std::string_view canvasname, Mathf::Vector2 Pos)
{
	auto curScene = SceneManagers->GetActiveScene();
	if (curScene == nullptr) return nullptr;

    if (curScene->GetCanvases().empty())
            MakeCanvas();
    GameObject* canvas = FindCanvasName(canvasname);
    if (canvas == nullptr)
    {
        std::cout << "해당 이름의 캔버스가 없습니다." << std::endl;
        return nullptr;
    }
    return MakeSpriteSheet(name, spriteSheetPath, canvas, Pos);
}

void UIManager::DeleteCanvas(const std::shared_ptr<GameObject>& canvas)
{
    if (!canvas) return;

	auto curScene = canvas->GetScene();
	if (curScene == nullptr) return;

	curScene->RemoveCanvas(canvas);
}

void UIManager::CheckInput()
{
	auto curCanvasObj = CurCanvas.lock();
	float tick = Time->GetElapsedSeconds();

	elapsed += tick;
	
	if (!curCanvasObj) return;

	auto canvasScene = curCanvasObj->GetScene();
	auto activeScene = SceneManagers->GetActiveScene();
	if (canvasScene != activeScene)
	{
		//activeScene가 바뀌었을 때 CurCanvas를 nullptr로 만들어서 다시 캔버스 찾도록 함
		CurCanvas.reset();
		return;
	}

	if (!isEnableUINavigation)
	{
		return;
	}

	Canvas* curCanvas = curCanvasObj->GetComponent<Canvas>();
	if (InputManagement->IsMouseButtonReleased(MouseKey::LEFT))
	{
		for (auto& uiObj : curCanvas->UIObjs)
		{
			auto uiObjPtr = uiObj.lock();
			if (uiObjPtr)
			{
				UIComponent* UI = uiObjPtr->GetComponent<UIComponent>();
				if (UI && false == UI->IsEnabled()) continue;
				UIButton* btn = uiObjPtr->GetComponent<UIButton>();
				if (btn == nullptr || btn->CheckClick(InputManagement->GetMousePos()) == false) continue;
				btn->Click();
			}
			break;
		}
	}

	//0을 1p,2p로 바꾸거나 둘다따로 주게 수정필요, 이동마다 대기시간 딜레이 주기 한번에 여러개 못넘어가게 *****
	//TODO : 추가로 특정 상황일때 비활성화 할 수 있도록 처리도 해야할 거 같은데?
	Mathf::Vector2 stickLP1 = InputManagement->GetControllerThumbL(0);
	Mathf::Vector2 stickLP2 = InputManagement->GetControllerThumbL(1);
	auto selectUI = SelectUI.lock();
	if (selectUI)
	{
		if(selectUI->GetComponent<ImageComponent>()->IsNavLock()) return;

		if(0.2 < elapsed)
		{
			if (stickLP1.x > 0.5 || stickLP2.x > 0.5)
			{
				auto navi = selectUI->GetComponent<ImageComponent>()->GetNextNavi(Direction::Right);
				if (navi)
				{
					SelectUI = navi->shared_from_this();
					elapsed = 0;
				}
			}
			if (stickLP1.x < -0.5 || stickLP2.x < -0.5)
			{
				auto navi = selectUI->GetComponent<ImageComponent>()->GetNextNavi(Direction::Left);
				if (navi)
				{
					SelectUI = navi->shared_from_this();
					elapsed = 0;
				}
			}
			if (stickLP1.y > 0.5 || stickLP2.y > 0.5)
			{
				auto navi = selectUI->GetComponent<ImageComponent>()->GetNextNavi(Direction::Up);
				if (navi)
				{
					SelectUI = navi->shared_from_this();
					elapsed = 0;
				}
			}
			if (stickLP1.y < -0.5 || stickLP2.y < -0.5)
			{
				auto navi = selectUI->GetComponent<ImageComponent>()->GetNextNavi(Direction::Down);
				if (navi)
				{
					SelectUI = navi->shared_from_this();
					elapsed = 0;
				}
			}
		}

		if (InputManagement->IsControllerButtonReleased(0, ControllerButton::A) ||
			InputManagement->IsControllerButtonReleased(1, ControllerButton::A))
		{
			auto button = selectUI->GetComponent<UIButton>();
			if(button)
				button->Click();
		}
	}
}

GameObject* UIManager::FindCanvasName(std::string_view name)
{
	auto curScene = SceneManagers->GetActiveScene();
	if (curScene == nullptr) return nullptr;
	auto CanvasObj = curScene->FindCanvasName(name);
	return CanvasObj.get();
}

GameObject* UIManager::FindCanvasIndex(int index)
{
	auto curScene = SceneManagers->GetActiveScene();
	if (curScene == nullptr) return nullptr;
	auto CanvasObj = curScene->FindCanvasIndex(index);
	return CanvasObj.get();
}

GameObject* UIManager::FindCanvasName(const std::shared_ptr<GameObject>& obj, std::string_view name)
{
	auto curScene = obj->GetScene();
	if (curScene == nullptr) return nullptr;
	auto CanvasObj = curScene->FindCanvasName(name);

	return CanvasObj.get();
}

GameObject* UIManager::FindCanvasIndex(const std::shared_ptr<GameObject>& obj, int index)
{
	auto curScene = obj->GetScene();
	if (curScene == nullptr) return nullptr;

	auto CanvasObj = curScene->FindCanvasIndex(index);

	return CanvasObj.get();
}

void UIManager::Update()
{
	SortCanvas();

	auto curScene = SceneManagers->GetActiveScene();
	if (curScene != nullptr || CurCanvas.expired())
	{
		auto& canvases = curScene->GetCanvases();

		for (int i = static_cast<int>(canvases.size()) - 1; i >= 0; i--)
		{
			auto canvasPtr = canvases[i].lock();
			if (!canvasPtr)
			{
				canvases.erase(canvases.begin() + i);
				continue;
			}
			auto canvas = canvasPtr->GetComponent<Canvas>();
			if (!canvas->IsEnabled()) continue;
			
			if (CurCanvas.lock() != canvasPtr)
			{
				CurCanvas = canvasPtr;
				SelectUI = canvas->GetFrontUIObject();
			}
			break;
		}

		for (auto& image : Images)
		{
			if (!image->isDeserialized)
			{
				image->DeserializeNavi();
			}
		}

		for (auto& text : Texts)
		{
			if (!text->isDeserialized)
			{
				text->DeserializeNavi();
			}
		}

		for (auto& spriteSheet : SpriteSheets)
		{
			if (!spriteSheet->isDeserialized)
			{
				spriteSheet->DeserializeNavi();
			}
		}

		CheckInput();
	}
}

void UIManager::SortCanvas()
{
	auto curScene = SceneManagers->GetActiveScene();
	if (curScene == nullptr) return;
	//정렬이 필요없다면 리턴

	if (needSort == false)
	{
		return;
	}
	else
	{
		auto& canvases = curScene->GetCanvases();
		std::erase_if(canvases, [](const std::weak_ptr<GameObject>& canvas) {
			return canvas.expired() || canvas.lock()->GetComponent<Canvas>()->IsDestroyMark();
		});

		std::ranges::sort(canvases, [](const std::weak_ptr<GameObject>& a, const std::weak_ptr<GameObject>& b) {
			auto aCanvas = a.lock();
			auto bCanvas = b.lock();
			if (aCanvas && bCanvas)
				return aCanvas->GetComponent<Canvas>()->GetCanvasOrder() < bCanvas->GetComponent<Canvas>()->GetCanvasOrder();
			return false; // 둘 중 하나가 nullptr인 경우 false 반환
		});

	}
	needSort = false;
}

void UIManager::RegisterImageComponent(ImageComponent* image)
{
	if (image)
		Images.push_back(image);
}

void UIManager::RegisterTextComponent(TextComponent* text)
{
	if (text)
		Texts.push_back(text);
}

void UIManager::RegisterSpriteSheetComponent(SpriteSheetComponent* spriteSheet)
{
	if (spriteSheet)
		SpriteSheets.push_back(spriteSheet);
}

void UIManager::UnregisterImageComponent(ImageComponent* image)
{
	std::erase(Images, image);
}

void UIManager::UnregisterTextComponent(TextComponent* text)
{
	std::erase(Texts, text);
}

void UIManager::UnregisterSpriteSheetComponent(SpriteSheetComponent* spriteSheet)
{
	std::erase(SpriteSheets, spriteSheet);
}
