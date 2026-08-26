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
#include "RHI/ScreenSizedResource.h"
#include <algorithm>

namespace
{
    // 선택 상태는 **활성 씬 기준**으로만 뜻이 있다. 다른 씬의 것이면 nullptr가
    // 정답이다 — Scene::Resolve가 sceneId로 그것을 그 자리에서 판정한다(E5-0).
    Entity* ResolveInActiveScene(const EntityHandle& handle)
    {
        if (!handle.IsValid()) return nullptr;
        Scene* scene = SceneManagers->GetActiveScene();
        return (nullptr != scene) ? scene->Resolve(handle) : nullptr;
    }

    EntityHandle HandleOfObject(Entity* object)
    {
        if (nullptr == object) return EntityHandle{};
        Scene* scene = object->GetScene();
        return (nullptr != scene) ? scene->HandleOf(object->m_index) : EntityHandle{};
    }
}

Entity* UIManager::GetCurCanvas() const { return ResolveInActiveScene(CurCanvas); }
Entity* UIManager::GetSelectUI() const { return ResolveInActiveScene(SelectUI); }
void UIManager::SetCurCanvas(Entity* canvas) { CurCanvas = HandleOfObject(canvas); }
void UIManager::SetSelectUI(Entity* ui) { SelectUI = HandleOfObject(ui); }

Entity* UIManager::MakeCanvas(std::string_view name)
{
	if(auto existingCanvas = FindCanvasName(name); existingCanvas )
	{
		std::cout << "Canvas with name '" << name << "' already exists." << std::endl;
		return nullptr;
	}

	auto curScene = SceneManagers->GetActiveScene();
	if (curScene == nullptr) return nullptr;

	auto newObj = SceneManagers->GetActiveScene()->CreateEntity(name, GameObjectType::Canvas);
	auto can = newObj->AddComponent<Canvas>();

    if (auto* rect = newObj->GetComponent<RectTransformComponent>())
    {
        // 화면 크기는 화면 크기 버스에서 온다. 예전에는 DX11 전역
        // (g_ClientRect)을 읽었는데, D4에서 나머지 판독부는 전부 버스로
        // 옮겼고 여기와 UIButton 둘만 남아 출처가 갈려 있었다.
        const std::array<float, 2> screenSize{
            static_cast<float>(ScreenResizeBus::Get().GetWidth()),
            static_cast<float>(ScreenResizeBus::Get().GetHeight())
        };
        rect->SetAnchoredPosition({ 0.0f, 0.0f });
        rect->SetSizeDelta({ screenSize[0], screenSize[1] });
        // 다음 프레임의 캔버스 구동이 어차피 화면 크기로 덮지만, 그 사이에 이 rect를
        // 읽는 코드가 (0,0,W,H)라는 잘못된 원점을 보지 않도록 여기서 맞춰 둔다.
		SceneManagers->GetActiveScene()->LayoutUISubtree(newObj);
    }

	// 캐시 기록은 Scene::AddCanvas 한 곳만 지난다. 예전에는 여기와 UIManager::AddCanvas
	// 둘이 각자 써 넣었고, 키가 갈렸다 — 여기는 **요청한** 이름(name)을, 저기는
	// 확정된 이름(canvas->ToString())을 썼다. CreateEntity가 충돌 회피로 개명하면
	// 그 캔버스는 자기 이름으로 찾히지 않는다.
	curScene->AddCanvas(newObj);

	needSort = true;
	return newObj;
}

void UIManager::AddCanvas(Entity* canvas)
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

	if (auto it = CanvasMap.find(canvasName); it != CanvasMap.end())
	{
		if (curScene->Resolve(it->second))
		{
			std::cout << "Canvas with name '" << canvasName << "' already exists." << std::endl;
			return;
		}

		// 이름은 잡혀 있는데 해석이 안 된다 = 캐시가 낡았다. 이름과 목록의 짝을
		// 함께 걷어야 다음 등록이 막히지 않는다.
		CanvasMap.erase(it);
		std::erase_if(Canvases, [&](const EntityHandle& handle) {
			Entity* obj = curScene->Resolve(handle);
			return !obj || obj->ToString() == canvasName;
		});
	}

	curScene->AddCanvas(canvas);
	needSort = true;
}

Entity* UIManager::MakeImage(std::string_view name, const std::shared_ptr<Texture>& texture, Entity* canvas, math::vector2 Pos)
{
	auto curScene = SceneManagers->GetActiveScene();
	if (curScene == nullptr) return nullptr;

	if (curScene->GetCanvases().empty())
		MakeCanvas();

	// MakeCanvas는 이름 충돌 등으로 실패할 수 있다(널 반환). 그 상태로 front()에
	// 가면 빈 벡터 접근으로 즉사한다 — GetFrontUIObject에서 실제로 겪은 유형(6-5).
	if (curScene->GetCanvases().empty()) return nullptr;

	if (!canvas)
	{
		canvas = curScene->Resolve(curScene->GetCanvases().front());
		if (!canvas) return nullptr;
	}
	auto canvasCom = canvas->GetComponent<Canvas>();
	auto canvasRect = canvas->GetComponent<RectTransformComponent>();
	if(!canvasCom || !canvasRect)
	{
		std::cout << "This Obj Not Canvas" << std::endl;
		return nullptr;
	}
    auto newImage = SceneManagers->GetActiveScene()->CreateEntity(name, GameObjectType::UI, canvas->m_index);
    if (auto* rect = newImage->GetComponent<RectTransformComponent>())
    {
        rect->SetLayoutScale(canvasRect->GetLayoutScale());
		rect->SetAnchorPreset(AnchorPreset::MiddleCenter);
		rect->SetPivot({ 0.5f, 0.5f });
        rect->SetAnchoredPosition(Pos);
        SceneManagers->GetActiveScene()->LayoutUISubtree(rect->GetOwner());
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

Entity* UIManager::MakeImage(std::string_view name, const std::shared_ptr<Texture>& texture, std::string_view canvasname, math::vector2 Pos)
{
	auto curScene = SceneManagers->GetActiveScene();
	if (curScene == nullptr) return nullptr;

	if (curScene->GetCanvases().empty())
            MakeCanvas();

    Entity* canvas = FindCanvasName(canvasname);
    if (canvas == nullptr)
    {
        std::cout << "해당 이름의 캔버스가 없습니다." << std::endl;
        return nullptr;
    }

    return MakeImage(name, texture, canvas, Pos);
}

Entity* UIManager::MakeButton(std::string_view name, const std::shared_ptr<Texture>& texture, std::function<void()> clickfun, math::vector2 Pos, Entity* canvas)
{
	auto curScene = SceneManagers->GetActiveScene();
	if (curScene == nullptr) return nullptr;

	auto& Canvases = curScene->GetCanvases();

	if (Canvases.empty())
	{
		MakeCanvas();
	}

	// MakeCanvas 실패 시 빈 벡터 front() 방지(6-5).
	if (Canvases.empty()) return nullptr;

    if (!canvas)
    {
        canvas = curScene->Resolve(Canvases.front());
        if (!canvas) return nullptr;
    }

    auto canvasCom = canvas->GetComponent<Canvas>();
    auto canvasRect = canvas->GetComponent<RectTransformComponent>();
    if (!canvasCom || !canvasRect)
    {
        std::cout << "This Obj Not Canvas" << std::endl;
        return nullptr;
    }

    auto newButton = SceneManagers->GetActiveScene()->CreateEntity(name, GameObjectType::UI, canvas->m_index);
    if (auto* rect = newButton->GetComponent<RectTransformComponent>())
    {
        rect->SetLayoutScale(canvasRect->GetLayoutScale());
        rect->SetAnchorPreset(AnchorPreset::MiddleCenter);
        rect->SetPivot({ 0.5f, 0.5f });
        rect->SetAnchoredPosition(Pos);
        SceneManagers->GetActiveScene()->LayoutUISubtree(rect->GetOwner());
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

Entity* UIManager::MakeButton(std::string_view name, const std::shared_ptr<Texture>& texture, std::function<void()> clickfun, std::string_view canvasname,  math::vector2 Pos)
{
	auto curScene = SceneManagers->GetActiveScene();
	if (curScene == nullptr) return nullptr;

	if (curScene->GetCanvases().empty())
         MakeCanvas();

    Entity* canvas = FindCanvasName(canvasname);
    if (canvas == nullptr)
    {
        std::cout << "해당 이름의 캔버스가 없습니다." << std::endl;
        return nullptr;
    }

    return MakeButton(name, texture, clickfun, Pos, canvas);
}

Entity* UIManager::MakeText(std::string_view name, file::path FontName, Entity* canvas, math::vector2 Pos)
{
	auto curScene = SceneManagers->GetActiveScene();
	if (curScene == nullptr) return nullptr;

	auto& Canvases = curScene->GetCanvases();

	if (Canvases.empty())
		MakeCanvas();

	// MakeCanvas 실패 시 빈 벡터 front() 방지(6-5).
	if (Canvases.empty()) return nullptr;

	if (!canvas)
	{
		canvas = curScene->Resolve(Canvases.front());
		if (!canvas) return nullptr;
	}
	auto canvasCom = canvas->GetComponent<Canvas>();
	auto canvasRect = canvas->GetComponent<RectTransformComponent>();
	if (!canvasCom || !canvasRect)
	{
		std::cout << "This Obj Not Canvas" << std::endl;
		return nullptr;
	}

	auto newText = SceneManagers->GetActiveScene()->CreateEntity(name, GameObjectType::UI, canvas->m_index);
	if (auto* rect = newText->GetComponent<RectTransformComponent>())
	{
		rect->SetLayoutScale(canvasRect->GetLayoutScale());
		rect->SetAnchorPreset(AnchorPreset::MiddleCenter);
		rect->SetPivot({ 0.5f, 0.5f });
		rect->SetAnchoredPosition(Pos);
		SceneManagers->GetActiveScene()->LayoutUISubtree(rect->GetOwner());
	}
	TextComponent* textComp = newText->AddComponent<TextComponent>();
	textComp->SetFont(FontName);
	textComp->isDeserialized = true;
	canvasCom->AddUIObject(newText);

	return newText;
}

Entity* UIManager::MakeText(std::string_view name, file::path FontName, std::string_view canvasname, math::vector2 Pos)
{
	auto curScene = SceneManagers->GetActiveScene();
	if (curScene == nullptr) return nullptr;

	if (curScene->GetCanvases().empty())
            MakeCanvas();
    Entity* canvas = FindCanvasName(canvasname);
    if (canvas == nullptr)
    {
        std::cout << "해당 이름의 캔버스가 없습니다." << std::endl;
        return nullptr;
    }
    return MakeText(name, FontName, canvas, Pos);
}

Entity* UIManager::MakeSpriteSheet(std::string_view name, const file::path& spriteSheetPath, Entity* canvas, math::vector2 Pos)
{
	auto curScene = SceneManagers->GetActiveScene();
	if (curScene == nullptr) return nullptr;

	if (curScene->GetCanvases().empty())
        MakeCanvas();

	// MakeCanvas 실패 시 빈 벡터 front() 방지(6-5).
	if (curScene->GetCanvases().empty()) return nullptr;

    if (!canvas)
    {
        canvas = curScene->Resolve(curScene->GetCanvases().front());
        if (!canvas) return nullptr;
    }
    auto canvasCom = canvas->GetComponent<Canvas>();
    auto canvasRect = canvas->GetComponent<RectTransformComponent>();
    if (!canvasCom || !canvasRect)
    {
        std::cout << "This Obj Not Canvas" << std::endl;
        return nullptr;
    }

    auto newSpriteSheet = SceneManagers->GetActiveScene()->CreateEntity(name, GameObjectType::UI, canvas->m_index);
    if (auto* rect = newSpriteSheet->GetComponent<RectTransformComponent>())
    {
        rect->SetLayoutScale(canvasRect->GetLayoutScale());
        rect->SetAnchorPreset(AnchorPreset::MiddleCenter);
        rect->SetPivot({ 0.5f, 0.5f });
        rect->SetAnchoredPosition(Pos);
        SceneManagers->GetActiveScene()->LayoutUISubtree(rect->GetOwner());
    }
	SpriteSheetComponent* spriteSheetCom = newSpriteSheet->AddComponent<SpriteSheetComponent>();
	spriteSheetCom->LoadSpriteSheet(spriteSheetPath);
	spriteSheetCom->isDeserialized = true;
    canvasCom->AddUIObject(newSpriteSheet);
    return newSpriteSheet;
}

Entity* UIManager::MakeSpriteSheet(std::string_view name, const file::path& spriteSheetPath, std::string_view canvasname, math::vector2 Pos)
{
	auto curScene = SceneManagers->GetActiveScene();
	if (curScene == nullptr) return nullptr;

    if (curScene->GetCanvases().empty())
            MakeCanvas();
    Entity* canvas = FindCanvasName(canvasname);
    if (canvas == nullptr)
    {
        std::cout << "해당 이름의 캔버스가 없습니다." << std::endl;
        return nullptr;
    }
    return MakeSpriteSheet(name, spriteSheetPath, canvas, Pos);
}

void UIManager::DeleteCanvas(Entity* canvas)
{
    if (!canvas) return;

	auto curScene = canvas->GetScene();
	if (curScene == nullptr) return;

	curScene->RemoveCanvas(canvas);
}

void UIManager::CheckInput()
{
	if (SceneManagers->IsSceneLoading()) return;

	// 활성 씬의 것이 아니면 여기서 이미 nullptr다 — 예전에는 lock한 뒤 그
	// GetScene()을 활성 씬과 손으로 비교해 리셋했다(트랙 E5-R3).
	Entity* curCanvasObj = GetCurCanvas();
	float tick = Time->GetElapsedSeconds();

	elapsed += tick;

	if (!curCanvasObj) return;

	if (!isEnableUINavigation)
	{
		return;
	}

	Canvas* curCanvas = curCanvasObj->GetComponent<Canvas>();
	if (InputManagement->IsMouseButtonReleased(MouseKey::LEFT))
	{
		Scene* scene = curCanvasObj->GetScene();
		for (const EntityHandle& uiHandle : curCanvas->UIObjs)
		{
			Entity* uiObj = scene ? scene->Resolve(uiHandle) : nullptr;
			if (!uiObj) continue;
			UIComponent* UI = uiObj->GetComponent<UIComponent>();
			if (UI && false == UI->IsEnabled()) continue;
			UIButton* btn = uiObj->GetComponent<UIButton>();
			if (btn == nullptr || btn->CheckClick(InputManagement->GetMousePos()) == false) continue;
			btn->Click();
			break;
		}
	}

	//0을 1p,2p로 바꾸거나 둘다따로 주게 수정필요, 이동마다 대기시간 딜레이 주기 한번에 여러개 못넘어가게 *****
	//TODO : 추가로 특정 상황일때 비활성화 할 수 있도록 처리도 해야할 거 같은데?
	math::vector2 stickLP1 = InputManagement->GetControllerThumbL(0);
	math::vector2 stickLP2 = InputManagement->GetControllerThumbL(1);
	Entity* selectUI = GetSelectUI();
	if (selectUI)
	{
		auto imageComponent = selectUI->GetComponent<ImageComponent>();
		if(!imageComponent || imageComponent->IsNavLock()) return;

		if(0.2 < elapsed)
		{
			if (stickLP1.x > 0.5 || stickLP2.x > 0.5)
			{
				auto navi = imageComponent->GetNextNavi(Direction::Right);
				if (navi)
				{
					SetSelectUI(navi);
					elapsed = 0;
				}
			}
			if (stickLP1.x < -0.5 || stickLP2.x < -0.5)
			{
				auto navi = imageComponent->GetNextNavi(Direction::Left);
				if (navi)
				{
					SetSelectUI(navi);
					elapsed = 0;
				}
			}
			if (stickLP1.y > 0.5 || stickLP2.y > 0.5)
			{
				auto navi = imageComponent->GetNextNavi(Direction::Up);
				if (navi)
				{
					SetSelectUI(navi);
					elapsed = 0;
				}
			}
			if (stickLP1.y < -0.5 || stickLP2.y < -0.5)
			{
				auto navi = imageComponent->GetNextNavi(Direction::Down);
				if (navi)
				{
					SetSelectUI(navi);
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

Entity* UIManager::FindCanvasName(std::string_view name)
{
	auto curScene = SceneManagers->GetActiveScene();
	if (curScene == nullptr) return nullptr;
	return curScene->FindCanvasName(name);
}

Entity* UIManager::FindCanvasName(Entity* obj, std::string_view name)
{
	if (!obj) return nullptr;
	auto curScene = obj->GetScene();
	if (curScene == nullptr) return nullptr;
	return curScene->FindCanvasName(name);
}

void UIManager::Update()
{
	SortCanvas();

	auto curScene = SceneManagers->GetActiveScene();

	// 원래 (curScene != nullptr || CurCanvas.expired())였는데, 씬이 널이어도
	// CurCanvas가 만료돼 있으면 참이 되어 바로 아래에서 널 씬을 역참조했다.
	// 본문 전체가 씬을 쓰므로 씬 존재가 유일한 진짜 전제 조건이다.
	if (curScene != nullptr)
	{
		auto& canvases = curScene->GetCanvases();

		for (int i = static_cast<int>(canvases.size()) - 1; i >= 0; i--)
		{
			Entity* canvasObj = curScene->Resolve(canvases[i]);
			if (!canvasObj)
			{
				canvases.erase(canvases.begin() + i);
				continue;
			}
			auto canvas = canvasObj->GetComponent<Canvas>();
			// 핸들이 유효해도 컴포넌트가 있다는 보장은 없다 — 옛 코드는 여기서
			// 널을 그대로 역참조했다.
			if (!canvas || !canvas->IsEnabled()) continue;

			if (GetCurCanvas() != canvasObj)
			{
				SetCurCanvas(canvasObj);
				SetSelectUI(canvas->GetFrontUIObject());
			}
			break;
		}

		// 캔버스 연결의 단일 지연 지점(6-3).
		//
		// 예전에는 ComponentFactory의 타입별 분기가 역직렬화 도중 연결을 수행해서,
		// 캔버스가 UI보다 늦게 복원되면 연결이 실패하고(순서 의존) 실패 경로가
		// 곧 크래시였다. 여기서는 모든 오브젝트가 이미 존재하는 프레임 경계에
		// 아직 연결되지 않은 것만 잇는다 — 로드 경로가 몇 개든 전부 이 한 곳을 지난다.
		// AddUIObject가 같은 오브젝트의 UIButton까지 함께 연결하므로 버튼도 처리된다.

		// UI가 소속될 캔버스를 자기 조상 계층에서 찾는다(자기 자신 포함).
		//
		// 관계가 진실이고 이름은 폴백이다(6-2 완결). 이름 우선이면 두 가지가 어긋난다:
		//  · 소환 시 이름 충돌로 캔버스가 개명되면("Canvas"→"Canvas (1)") 자식들이
		//    엉뚱한 동명 캔버스에 붙는다 — 실측으로 확인한 오연결.
		//  · 실제 콘텐츠(HPCanvas.prefab)의 이름 필드가 저작 당시 낡은 값("Canvas")을
		//    담고 있어도 계층은 정확했다. 관계 우선이면 낡은 이름이 저절로 무해해진다.
		auto findAncestorCanvas = [](Entity* node) -> Canvas*
		{
			// 계층이 꼬여 순환이 생겨도 멈추도록 깊이를 제한한다.
			constexpr int kMaxDepth = 64;

			for (int depth = 0; nullptr != node && depth < kMaxDepth; ++depth)
			{
				if (Canvas* canvas = node->GetComponent<Canvas>()) return canvas;

				const Entity::Index parentIndex = node->GetParentIndex();
				if (Entity::INVALID_INDEX == parentIndex) break;

				node = node->OwnerSceneFindIndex(parentIndex);
			}
			return nullptr;
		};

		auto tryLink = [&](UIComponent* ui)
		{
			if (nullptr == ui || nullptr != ui->GetOwnerCanvas()) return;

			Entity* owner = ui->GetOwner();
			if (nullptr == owner || owner->GetScene() != curScene) return;

			// 1순위: 조상 계층. 소환 과정에서 캔버스가 개명됐어도 정확히 자기 캔버스를 찾는다.
			Canvas* canvas = findAncestorCanvas(owner);

			// 2순위: 직렬화된 이름. 계층상 캔버스 아래에 있지 않은 UI를 위한 하위 호환이다.
			if (nullptr == canvas && !ui->m_ownerCanvasName.empty())
			{
				Entity* canvasObj = FindCanvasName(owner, ui->m_ownerCanvasName);
				canvas = (nullptr != canvasObj) ? canvasObj->GetComponent<Canvas>() : nullptr;
			}

			if (nullptr != canvas)
			{
				// SetCanvas가 이름을 실제 캔버스 기준으로 다시 기록하므로,
				// 낡은 이름은 다음 저장 때 저절로 고쳐진다.
				canvas->AddUIObject(owner);
			}
			else if (!ui->m_ownerCanvasName.empty() && !ui->m_canvasLinkLogged)
			{
				// 둘 다 실패하면 다음 프레임에 다시 시도한다(캔버스가 나중에 생길 수 있다).
				// 경고는 컴포넌트당 한 번만 — 매 프레임 재시도라 그대로 두면 로그가 폭주한다.
				// 이름조차 없는 미연결 UI는 경고하지 않는다(런타임에 갓 만든 정상 상태).
				ui->m_canvasLinkLogged = true;
				Debug->LogWarning("UI '" + owner->m_name.ToString() +
					"' 가 캔버스 '" + ui->m_ownerCanvasName + "' 를 찾지 못했습니다.");
			}
		};

		for (auto& image : Images)
		{
			tryLink(image);
			if (!image->isDeserialized)
			{
				image->DeserializeNavi();
			}
		}

		for (auto& text : Texts)
		{
			tryLink(text);
			if (!text->isDeserialized)
			{
				text->DeserializeNavi();
			}
		}

		for (auto& spriteSheet : SpriteSheets)
		{
			tryLink(spriteSheet);
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
		// 캐시 청소의 정본 지점. 여기서 해석 실패·컴포넌트 부재를 함께 걷어내므로
		// 아래 정렬 비교자는 둘 다 살아 있다고 가정할 수 있다(비교자가 널을 만나
		// false를 반환하면 엄격 약순서가 깨져 UB다).
		auto& canvases = curScene->GetCanvases();
		std::erase_if(canvases, [curScene](const EntityHandle& handle) {
			Entity* obj = curScene->Resolve(handle);
			if (!obj) return true;
			Canvas* canvas = obj->GetComponent<Canvas>();
			return !canvas || canvas->IsDestroyMark();
		});

		std::ranges::sort(canvases, [curScene](const EntityHandle& a, const EntityHandle& b) {
			Entity* aObj = curScene->Resolve(a);
			Entity* bObj = curScene->Resolve(b);
			if (!aObj || !bObj) return false;
			Canvas* aCanvas = aObj->GetComponent<Canvas>();
			Canvas* bCanvas = bObj->GetComponent<Canvas>();
			if (!aCanvas || !bCanvas) return false;
			return aCanvas->GetCanvasOrder() < bCanvas->GetCanvasOrder();
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


