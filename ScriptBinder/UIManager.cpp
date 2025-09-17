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
	if( auto existingCanvas = FindCanvasName(name); existingCanvas )
	{
		std::cout << "Canvas with name '" << name << "' already exists." << std::endl;
		return nullptr;
	}

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

	std::string canvasName = canvas->ToString();
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

	canvasCom->AddUIObject(newImage);
	

	return newImage;
}

std::shared_ptr<GameObject> UIManager::MakeImage(std::string_view name, const std::shared_ptr<Texture>& texture, std::string_view canvasname, Mathf::Vector2 Pos)
{
    if (Canvases.empty())
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

    auto newButton = SceneManagers->GetActiveScene()->CreateGameObject(name, GameObjectType::UI, canvas->m_index);
    if (auto* rect = newButton->GetComponent<RectTransformComponent>())
    {
            rect->SetAnchorPreset(AnchorPreset::MiddleCenter);
            rect->SetPivot({ 0.5f, 0.5f });
            rect->SetAnchoredPosition(Pos);
            rect->UpdateLayout(canvasRect->GetWorldRect());
    }

    if (texture == nullptr)
    {
            newButton->AddComponent<ImageComponent>();
    }
    else
    {
            newButton->AddComponent<ImageComponent>()->Load(texture);
    }

    auto component = newButton->AddComponent<UIButton>();
    component->SetClickFunction(clickfun);

    canvasCom->AddUIObject(newButton);

    return newButton;
}

std::shared_ptr<GameObject> UIManager::MakeButton(std::string_view name, const std::shared_ptr<Texture>& texture, std::function<void()> clickfun, std::string_view canvasname,  Mathf::Vector2 Pos)
{
    if (Canvases.empty())
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

    newText->AddComponent<TextComponent>()->SetFont(FontName);
    canvasCom->AddUIObject(newText);

    return newText;
}

std::shared_ptr<GameObject> UIManager::MakeText(std::string_view name, file::path FontName, std::string_view canvasname, Mathf::Vector2 Pos)
{
        if (Canvases.empty())
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

        auto newSpriteSheet = SceneManagers->GetActiveScene()->CreateGameObject(name, GameObjectType::UI, canvas->m_index);
        if (auto* rect = newSpriteSheet->GetComponent<RectTransformComponent>())
        {
                rect->SetAnchorPreset(AnchorPreset::MiddleCenter);
                rect->SetPivot({ 0.5f, 0.5f });
                rect->SetAnchoredPosition(Pos);
                rect->UpdateLayout(canvasRect->GetWorldRect());
        }

        newSpriteSheet->AddComponent<SpriteSheetComponent>()->LoadSpriteSheet(spriteSheetPath);
        canvasCom->AddUIObject(newSpriteSheet);
        return newSpriteSheet;
}

std::shared_ptr<GameObject> UIManager::MakeSpriteSheet(std::string_view name, const file::path& spriteSheetPath, std::string_view canvasname, Mathf::Vector2 Pos)
{
        if (Canvases.empty())
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

        auto it = std::find_if(Canvases.begin(), Canvases.end(), [&](const std::weak_ptr<GameObject>& c)
        {
                return !c.expired() && c.lock() == canvas;
        });

        if (it != Canvases.end())
        {
                auto canvasCom = canvas->GetComponent<Canvas>();
                for (auto& uiObj : canvasCom->UIObjs)
                {
                        if (auto uiObjPtr = uiObj.lock())
                                uiObjPtr->Destroy();
                }
                canvasCom->UIObjs.clear();

                std::erase_if(Canvases, [&](const std::weak_ptr<GameObject>& c)
                        {
                                return c.expired() || c.lock() == canvas;
                        });

                std::erase_if(CanvasMap, [&](auto& pair)
                        {
                                auto sp = pair.second.lock();
                                return !sp || sp == canvas;
                        });

                canvas->Destroy();
        }
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
	Mathf::Vector2 stickL = InputManagement->GetControllerThumbL(0);
	auto selectUI = curCanvas->SelectUI.lock();
	if (selectUI)
	{
		if (stickL.x > 0.5)
		{
			auto navi = selectUI->GetComponent<ImageComponent>()->GetNextNavi(Direction::Right);
			if (navi)
			{
				curCanvas->SelectUI = navi->shared_from_this();
			}
		}
		if (stickL.x < -0.5)
		{
			auto navi = selectUI->GetComponent<ImageComponent>()->GetNextNavi(Direction::Left);
			if (navi)
			{
				curCanvas->SelectUI = navi->shared_from_this();
			}
		}

		if (InputManagement->IsControllerButtonReleased(0, ControllerButton::A))
		{
			auto button = selectUI->GetComponent<UIButton>();
			if(button)
				button->Click();
		}
	}
}

GameObject* UIManager::FindCanvasName(std::string_view name)
{
	auto it = CanvasMap.find(name.data());
	if (it != CanvasMap.end())
	{
		if (auto canvasObj = it->second.lock())
		{
			return canvasObj.get();
		}
		else
		{
			CanvasMap.erase(it);
			std::erase_if(Canvases, [&](const std::weak_ptr<GameObject>& canvas) 
			{
				auto c = canvas.lock();
				return !c || c->ToString() == name;
			});
			return nullptr;
		}
	}

	return nullptr;
}

GameObject* UIManager::FindCanvasIndex(int index)
{
	if (index < 0 || index >= Canvases.size())
		return nullptr;
	auto canvasObj = Canvases[index].lock();
	if (canvasObj)
		return canvasObj.get();
	else
	{
		Canvases.erase(Canvases.begin() + index);
		return nullptr;
	}
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
