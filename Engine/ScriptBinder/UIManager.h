#pragma once
#include "../Utility_Framework/Core.Minimal.h"
#include "ClassProperty.h"
#include "Entity.h"
#include "EntityHandle.h"
#include "Core.Property.h"
#include <stop_token>

class Canvas;
class Texture;
class ImageComponent;
class TextComponent;
class SpriteSheetComponent;
class UIManager : public Singleton<UIManager>
{
public:
	friend class Singleton<UIManager>;
	Core::Delegate<void, Mathf::Vector2> m_clickEvent;
	Entity* MakeCanvas(std::string_view name = "Canvas");

	void AddCanvas(Entity* canvas);
	void DeleteCanvas(Entity* canvas);

	Entity* MakeImage(std::string_view name, const std::shared_ptr<Texture>& texture,Entity* canvas = nullptr,Mathf::Vector2 Pos = { 0, 0 });
	Entity* MakeImage(std::string_view name, const std::shared_ptr<Texture>& texture, std::string_view canvasname, Mathf::Vector2 Pos = { 0, 0 });
	Entity* MakeButton(std::string_view name, const std::shared_ptr<Texture>& texture, std::function<void()> clickfun, Mathf::Vector2 Pos = { 0, 0 },Entity* canvas = nullptr);
	Entity* MakeButton(std::string_view name, const std::shared_ptr<Texture>& texture, std::function<void()> clickfun, std::string_view canvasname, Mathf::Vector2 Pos = { 0, 0 });
	Entity* MakeText(std::string_view name, file::path FontName, Entity* canvas = nullptr, Mathf::Vector2 Pos = { 0, 0 });
	Entity* MakeText(std::string_view name, file::path FontName, std::string_view canvasname, Mathf::Vector2 Pos = { 0, 0 });

	Entity* MakeSpriteSheet(std::string_view name, const file::path& spriteSheetPath, Entity* canvas = nullptr, Mathf::Vector2 Pos = { 0, 0 });
	Entity* MakeSpriteSheet(std::string_view name, const file::path& spriteSheetPath, std::string_view canvasname, Mathf::Vector2 Pos = { 0, 0 });

	void CheckInput();
	Entity* FindCanvasName(Entity* obj, std::string_view name);
	Entity* FindCanvasName(std::string_view name);
	void Update();
	
	void SortCanvas();
	void RegisterImageComponent(ImageComponent* image);
	void RegisterTextComponent(TextComponent* text);
	void RegisterSpriteSheetComponent(SpriteSheetComponent* spriteSheet);
	void UnregisterImageComponent(ImageComponent* image);
	void UnregisterTextComponent(TextComponent* text);
	void UnregisterSpriteSheetComponent(SpriteSheetComponent* spriteSheet);
	//UI 네비게이션 활성화 여부 및 프로퍼티 클래스 테스트용
	Property<bool> EnableUINavigation
	{
		.get = [this]() { return isEnableUINavigation; },
		.set = [this](bool able) { isEnableUINavigation = able; }
	};
	//UI 네비게이션 활성화 여부 레거시
	bool IsEnableUINavigation() const { return isEnableUINavigation; }
	void SetEnableUINavigation(bool able) { isEnableUINavigation = able; }

public:
	//캔버스 컴포넌트가 들어있는것만 들어가게끔
	std::vector<ImageComponent*>			Images;
	std::vector<TextComponent*>			Texts;
	std::vector<SpriteSheetComponent*>    SpriteSheets;
	// 이전 캔버스 / 현재 상호작용할 UI — **저장은 핸들, 사용은 해석**(트랙 E5-R3).
	//
	// 예전에는 weak_ptr였고 그 자리 주석이 "EntityHandle로 못 바꾼다"고 사유를
	// 적어 두었다: 활성 씬이 바뀐 뒤 옛 핸들의 index·generation이 새 씬의 무관한
	// 오브젝트와 **우연히 일치**해 유효한 것처럼 풀릴 수 있다는 것이었다.
	// **E5-0이 핸들에 sceneId를 넣으면서 그 사유가 사라졌다** — Scene::Resolve가
	// 씬 불일치를 세대 검사보다 먼저 거른다.
	//
	// 그래서 CheckInput이 손으로 하던 "활성 씬이 바뀌면 리셋"(오브젝트를 lock한 뒤
	// 그 GetScene()을 활성 씬과 비교)도 필요 없어졌다 — 구조가 대신한다.
	//
	// 해석 실패는 손실이 아니다. 이 선택 상태는 UIManager::Update가 매 프레임
	// 캔버스 목록에서 다시 세우므로 한 프레임 뒤 자가 복구된다. 관리 코드
	// 브릿지(ClrHost.cpp의 Api_UiNav_Get/SetSelected)도 이 접근자를 지난다.
	Entity* GetCurCanvas() const;
	Entity* GetSelectUI() const;
	void SetCurCanvas(Entity* canvas);
	void SetSelectUI(Entity* ui);
	void ClearCurCanvas() { CurCanvas = {}; }
	void ClearSelectUI() { SelectUI = {}; }

	bool needSort = false;
	float elapsed{};

private:
	// 소유가 아니라 선택 상태다. 수명은 Scene이 쥔다.
	EntityHandle CurCanvas{};
	EntityHandle SelectUI{};

	bool isEnableUINavigation = true;
};

static auto UIManagers = UIManager::GetInstance();

interface ICollision2D
{
	ICollision2D()
	{	
		m_clickEventHandle = UIManagers->m_clickEvent.AddLambda(
			[this](Mathf::Vector2 _mousePos)
			{
				CheckClick(_mousePos);
			});
	}
	virtual ~ICollision2D()
	{
		UIManagers->m_clickEvent.Remove(m_clickEventHandle);
	}

	virtual void CheckClick(Mathf::Vector2 _mousePos) = 0;
	Core::DelegateHandle m_clickEventHandle{};
};
//btn clilc -> collider들 확인 -> 가장맞는  ui함수 실행
//현재 선택 canvas -> 현재 선택가능한  ui중 하나 고르게끔
//canvas없는 단일은?

