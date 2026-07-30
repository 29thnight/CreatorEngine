#include "Canvas.h"
#include "GameObject.h"
#include "ImageComponent.h"
#include "UIManager.h"
#include "TextComponent.h"
#include "UIButton.h"
#include "SceneManager.h"
#include "RectTransformComponent.h"
#include "SpriteSheetComponent.h"
#include <algorithm>

Canvas::Canvas()
{
	m_typeID = TypeTrait::GUIDCreator::GetTypeID<Canvas>();
}

void Canvas::OnDestroy()
{
	Scene* scene = SceneManagers->GetActiveScene();
	if (scene != nullptr && m_pOwner->IsDestroyMark() && !UIManagers->CurCanvas.expired())
	{
		if (UIManagers->CurCanvas.lock() == m_pOwner->shared_from_this())
		{
			UIManagers->CurCanvas.reset();
		}
		UIManagers->DeleteCanvas(m_pOwner->shared_from_this());
	}
}

void Canvas::AddUIObject(std::shared_ptr<GameObject> obj)
{
	if (!obj) return;

	// 레지스트리 등록은 여기서 하지 않는다(6-1). 등록은 각 컴포넌트의 Awake가
	// 자기 수명에 맞춰 직접 한다 — 예전에는 캔버스 연결이 곧 등록이라, 연결이
	// 안 된 UI는 레지스트리에 없고 연결 경로가 실패하면 크래시로 이어졌다.
	// 여기는 "이 캔버스 소속"이라는 연결만 만든다. 직렬화용 이름 기록도
	// SetCanvas가 함께 처리한다(6-2).
	auto link = [this](UIComponent* ui)
	{
		if (nullptr != ui) ui->SetCanvas(this);
	};

	link(obj->GetComponent<ImageComponent>());
	link(obj->GetComponent<TextComponent>());
	link(obj->GetComponent<UIButton>());
	link(obj->GetComponent<SpriteSheetComponent>());

	// 같은 오브젝트를 두 번 연결해도 목록이 불어나지 않게 한다.
	const bool already = std::ranges::any_of(UIObjs, [&](const std::weak_ptr<GameObject>& w)
	{
		return w.lock() == obj;
	});
	if (!already) UIObjs.push_back(obj);
}

void Canvas::RemoveUIObject(GameObject* obj)
{
	// UI 컴포넌트가 파괴될 때 스스로 부른다(6-4). 예전에는 Canvas::Update가
	// 매 프레임 erase_if로 파괴된 것을 청소했다 — 폴링 대신 이벤트로 옮긴 것.
	if (nullptr == obj) return;

	std::erase_if(UIObjs, [&](const std::weak_ptr<GameObject>& w)
	{
		auto locked = w.lock();
		return !locked || locked.get() == obj;
	});
}


void Canvas::Update(float tick)
{
	// 여기 있던 두 폴링은 걷어 냈다(6-4).
	//  · 파괴된 UI 청소(erase_if) → UI 컴포넌트 OnDestroy가 RemoveUIObject를 부른다.
	//    원 작성자도 "UI Manager에서 통합으로 처리하자"라고 남겨 뒀던 자리다.
	//  · 이름 변경 감지 → 연결이 직접 참조가 되면서(6-2) 런타임에 이름을 맞출 필요가
	//    없어졌다. 직렬화용 이름은 SetCanvas 시점에 한 번 기록된다.
	if (PreCanvasOrder != CanvasOrder)
	{
		UIManagers->needSort = true;
		PreCanvasOrder = CanvasOrder;
	}
}

std::weak_ptr<GameObject> Canvas::GetFrontUIObject()
{
	// UI 자식이 하나도 없는 캔버스가 선택되는 순간 빈 벡터에 front()를 불러
	// __fastfail(0xC0000409)로 즉사했다. 런타임에 만든 캔버스는 항상 비어 있어서
	// component.add로 캔버스만 붙이고 재생하면 100% 재현됐다.
	if (UIObjs.empty()) return {};

	return UIObjs.front();
}



