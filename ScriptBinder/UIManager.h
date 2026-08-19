#pragma once
#include "../Utility_Framework/Core.Minimal.h"
#include "ClassProperty.h"
#include "GameObject.h"
#include "Core.Property.h"
#include <stop_token>
#include <DirectXTK/SpriteFont.h>

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
	std::shared_ptr<Entity> MakeCanvas(std::string_view name = "Canvas");

	void AddCanvas(std::shared_ptr<Entity> canvas);
	void DeleteCanvas(const std::shared_ptr<Entity>& canvas);

	std::shared_ptr<Entity> MakeImage(std::string_view name, const std::shared_ptr<Texture>& texture,Entity* canvas = nullptr,Mathf::Vector2 Pos = { 960, 540 });
	std::shared_ptr<Entity> MakeImage(std::string_view name, const std::shared_ptr<Texture>& texture, std::string_view canvasname, Mathf::Vector2 Pos = { 960, 540 });
	std::shared_ptr<Entity> MakeButton(std::string_view name, const std::shared_ptr<Texture>& texture, std::function<void()> clickfun, Mathf::Vector2 Pos = { 960, 540 },Entity* canvas = nullptr);
	std::shared_ptr<Entity> MakeButton(std::string_view name, const std::shared_ptr<Texture>& texture, std::function<void()> clickfun, std::string_view canvasname, Mathf::Vector2 Pos = { 960, 540 });
	std::shared_ptr<Entity> MakeText(std::string_view name, file::path FontName, Entity* canvas = nullptr, Mathf::Vector2 Pos = { 960, 540 });
	std::shared_ptr<Entity> MakeText(std::string_view name, file::path FontName, std::string_view canvasname, Mathf::Vector2 Pos = { 960, 540 });

	std::shared_ptr<Entity> MakeSpriteSheet(std::string_view name, const file::path& spriteSheetPath, Entity* canvas = nullptr, Mathf::Vector2 Pos = { 960, 540 });
	std::shared_ptr<Entity> MakeSpriteSheet(std::string_view name, const file::path& spriteSheetPath, std::string_view canvasname, Mathf::Vector2 Pos = { 960, 540 });

	void CheckInput();
	Entity* FindCanvasName(const std::shared_ptr<Entity>& obj, std::string_view name);
	Entity* FindCanvasIndex(const std::shared_ptr<Entity>& obj, int index);
	Entity* FindCanvasName(std::string_view name);
	Entity* FindCanvasIndex(int index);
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
	//이정 캔버스
	//현재 상호작용할 UI
	// EntityHandle(E5-a 검토)로 못 바꾼다 — UIManager는 씬에 묶이지 않는
	// 싱글턴이라 Resolve할 Scene*를 자체적으로 갖지 않는다. 활성 씬이 바뀐 뒤
	// SceneManagers->GetActiveScene()으로 풀면, 옛 핸들의 index·generation이
	// 새 씬의 무관한 오브젝트와 우연히 일치해도 유효한 것처럼 풀려 버린다(핸들
	// 자체는 씬을 식별 못 함 — PrefabUtility::InstanceRef가 Scene*를 같이 저장하는
	// 이유와 같다, PrefabUtility.h 참고). CheckInput의 "활성 씬이 바뀌면 리셋"
	// 로직(UIManager.cpp)도 지금은 lock()으로 실제 오브젝트를 얻은 뒤 그 GetScene()을
	// 비교하는 방식이라 이 문제를 자연히 피한다. 관리 코드 브릿지(ClrHost.cpp
	// Api_UiNav_GetSelected/SetSelected)도 씬 컨텍스트 없이 접근한다.
	std::weak_ptr<Entity> CurCanvas;
	std::weak_ptr<Entity> SelectUI;

	bool needSort = false;
	float elapsed{};

private:
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

