#pragma once
#include "AuthoringNodeView.h"
#include <mathematics/vector2.hpp>
#include <mathematics/vector3.hpp>
#include "Component.h"
#include "IRenderable.h"
#include "Canvas.h"
#include "Navigation.h"
#include "EntityHandle.h"

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
	void SetNavi(Direction dir, Entity* otherUI);
	void OnAddedToScene() override;
	void OnRemovingFromScene() override;
	// typed 직렬화기가 필드를 읽기 직전에 호출한다. 런타임 약참조를 현재 계층의
	// 로컬 경로로 재계산해, 링크 설정 뒤 reparent한 경우도 다음 로드에서 맞게 한다.
	void OnBeforeSerialize();
	// 구 Navigation.navObject(instanceID) 파일의 읽기 전용 승격. 새 파일에는
	// navObject를 쓰지 않고, 한 번 해석되면 OnBeforeSerialize가 새 경로로 치유한다.
	void LoadLegacyNavigation(const Authoring::NodeView& componentNode); // D3-a-4
	void DeserializeNavi();
	std::vector<Navigation> GetNavigations() const { return navigations; }
	Entity* GetNextNavi(Direction dir);
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
	math::vector3 pos{ 960, 540, 0 };
	int _layerorder{};

	UItype type = UItype::None;
	bool isDeserialized = false;
	bool isNavLocked = false;
	UIEffects uiEffects{};

	math::vector2 scale{ 1, 1 };

	std::string m_ownerCanvasName{};

	// 캔버스를 못 찾았다는 경고를 한 번만 내기 위한 플래그. 직렬화하지 않는다.
	bool m_canvasLinkLogged = false;
	std::vector<Navigation> navigations{};
private:
	// 런타임 해석 캐시. 디스크 정본은 source-relative hierarchy route이고,
	// 캐시는 현재 씬의 세대 검증 핸들이다. DDOL 이송에서는 OnAddedToScene이
	// 래치를 풀어 모든 노드가 새 씬에 붙은 다음 DeserializeNavi가 다시 채운다.
	std::array<EntityHandle, NavDirectionCount> navigation{};
	// U7 이전 파일에만 있는 navObject 값. 반영 대상이 아니며 로드 직후 한 번만
	// 사용한다. 구 프리팹은 Instantiate 전 YAML 복사본에서 로컬 경로로 승격되고,
	// 구 씬은 이 캐시로 한 번 해석한 뒤 다음 저장에서 새 경로만 쓴다.
	std::array<HashedGuid, NavDirectionCount> m_legacyNavigationIds{};

	bool UpdateNavigationRoute(Navigation& nav, Entity* target);
	Entity* ResolveNavigationRoute(const Navigation& nav) const;

	// 소속 캔버스는 현재 씬의 핸들 캐시다. UI 워커 직접 접근 경로는 철거됐으므로
	// Resolve는 게임 스레드에서만 일어난다. DDOL 이송 때 비운 뒤 UIManager의
	// 계층 우선 지연 연결이 새 씬 핸들로 복원한다.
	EntityHandle m_ownerCanvasObject{};

protected:
};


