#pragma once
#include "Component.h"
#include "IRenderable.h"
#include "Canvas.h"
#include "Navigation.h"

extern float MaxOreder;

//아직안씀 
enum class UItype : uint16_t
{
	Image,
	Text,
	None,
};


class UIComponent : public meta::identity<UIComponent, Component>
{
   public:
   static consteval auto reflect()
   {
       return meta::schema<Self>(
           meta::field<&Self::_layerorder>,
           meta::field<&Self::uiEffects>,
           meta::field<&Self::m_ownerCanvasName>,
           meta::field<&Self::navigations>);
   }
public:
	UIComponent(); 
	virtual ~UIComponent() = default;

   void SetCanvas(Canvas* canvas);
	// 캔버스가 이미 파괴됐으면 널이다 — 호출부는 항상 널을 각오해야 한다.
	Canvas* GetOwnerCanvas();
	void SetOrder(int index) { _layerorder = index; }
	int GetLayerOrder() const { return _layerorder; }
	void SetNavi(Direction dir, const std::shared_ptr<GameObject>& otherUI);
	void DeserializeNavi();
	std::vector<Navigation> GetNavigations() const { return navigations; }
	GameObject* GetNextNavi(Direction dir);
	bool IsNavigationThis();
	void SetNavLock(bool lock) { isNavLocked = lock; }
	bool IsNavLock() const { return isNavLocked; }


	static bool CompareLayerOrder(UIComponent* a, UIComponent* b)
	{
		if (a->_layerorder != b->_layerorder)
			return a->_layerorder < b->_layerorder;
		auto aCanvas = a->GetOwnerCanvas();
		auto bCanvas = b->GetOwnerCanvas();
		int aOrder = aCanvas ? aCanvas->GetCanvasOrder() : 0;
		int bOrder = bCanvas ? bCanvas->GetCanvasOrder() : 0;
		return aOrder < bOrder;
	}

public:
	Mathf::Vector3 pos{ 960, 540, 0 };
	int _layerorder{};

	UItype type = UItype::None;
	bool isDeserialized = false;
	bool isNavLocked = false;
	UIEffects uiEffects{};

	Mathf::Vector2 scale{ 1, 1 };

	std::string m_ownerCanvasName{};

	// 캔버스를 못 찾았다는 경고를 한 번만 내기 위한 플래그. 직렬화하지 않는다.
	bool m_canvasLinkLogged = false;
	std::vector<Navigation> navigations{};
private:
	std::array<std::weak_ptr<GameObject>, NavDirectionCount> navigation;

	// 소속 캔버스. 원시 포인터였는데 캔버스가 먼저 파괴되면 dangling이었다(6-2).
	// 캔버스의 GameObject를 약참조로 들고, GetOwnerCanvas가 매번 살아 있는지 확인한다.
	// 렌더 프록시가 워커 스레드에서 읽는 경로라 lock()의 원자성이 실질적인 안전판이다.
	std::weak_ptr<GameObject> m_ownerCanvasObject;

protected:
};


