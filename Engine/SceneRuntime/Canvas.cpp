#include "Canvas.h"
#include "Entity.h"
#include "ImageComponent.h"
#include "UIManager.h"
#include "TextComponent.h"
#include "UIButton.h"
#include "SceneManager.h"
#include "Scene.h"
#include "RectTransformComponent.h"
#include "SpriteSheetComponent.h"
#include "UITickSystem.h"
#include <algorithm>
#include <cmath>

Canvas::Canvas()
{
}

float Canvas::ComputeScaleFactor(const math::rect& screenRect) const
{
	// 배율이 0이나 음수가 되면 레이아웃이 통째로 무너지므로(역산에서 0으로 나눔)
	// 어떤 경로로도 그 값이 나가지 않게 한다.
	constexpr float kMinScale = 0.01f;

	if (CanvasScaleMode::ConstantPixelSize == ScaleMode)
	{
		return std::max(kMinScale, ScaleFactor);
	}

	if (ReferenceResolution.x <= 0.f || ReferenceResolution.y <= 0.f ||
		screenRect.width <= 0.f || screenRect.height <= 0.f)
	{
		return 1.f;
	}

	// uGUI와 같은 로그 공간 보간. 선형으로 섞으면 match가 0.5일 때 가로세로
	// 비율이 크게 다른 화면에서 한쪽으로 치우친다.
	const float logWidth = std::log2(screenRect.width / ReferenceResolution.x);
	const float logHeight = std::log2(screenRect.height / ReferenceResolution.y);
	const float match = std::clamp(MatchWidthOrHeight, 0.f, 1.f);
	const float weighted = logWidth + (logHeight - logWidth) * match;

	return std::max(kMinScale, std::exp2(weighted));
}

void Canvas::OnUninitializing()
{
	Scene* scene = SceneManagers->GetActiveScene();
	if (scene != nullptr && m_pOwner->IsDestroyMark())
	{
		if (UIManagers->GetCurCanvas() == m_pOwner)
		{
			UIManagers->ClearCurCanvas();
		}
		UIManagers->DeleteCanvas(m_pOwner);
	}
}

void Canvas::AddUIObject(Entity* obj)
{
	if (!obj) return;
	Scene* scene = obj->GetScene();
	const EntityHandle handle = scene ? scene->HandleOf(obj->m_index) : EntityHandle{};
	if (!handle.IsValid()) return;

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
	const bool already = std::ranges::any_of(UIObjs, [&](const EntityHandle& existing)
	{
		return existing == handle;
	});
	if (!already) UIObjs.push_back(handle);
}

void Canvas::RemoveUIObject(Entity* obj)
{
	// UI 컴포넌트가 파괴될 때 스스로 부른다(6-4). 예전에는 Canvas::Update가
	// 매 프레임 erase_if로 파괴된 것을 청소했다 — 폴링 대신 이벤트로 옮긴 것.
	if (nullptr == obj) return;

	Scene* scene = GetOwner() ? GetOwner()->GetScene() : nullptr;
	std::erase_if(UIObjs, [&](const EntityHandle& handle)
	{
		Entity* resolved = scene ? scene->Resolve(handle) : nullptr;
		return !resolved || resolved == obj;
	});
}


void Canvas::TickCanvasOrder(float tick)
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

// 레인 UI — UITickSystem 등록/해지. Awake/OnDestroy(컴포넌트당 1회 게이트)가
// 아니라 씬 편입/이탈 훅을 쓰는 이유는 UITickSystem.h 상단 주석 참조 — DDOL
// 오브젝트가 씬을 건널 때도 매번 다시 불려야 하기 때문이다. 실제 파괴 경로
// (PrefabUtility::ApplyComponentDiff·Scene::FlushPendingDestroy)도 OnDestroy
// 직전에 OnRemovingFromScene을 먼저 부르므로, 이 시스템에서 빠지는 시점이
// 항상 실 파괴보다 먼저다.
void Canvas::OnAddedToScene()
{
	UITickSystems->RegisterCanvas(this);

	// 씬의 캔버스 캐시도 **같은 훅**에서 갱신한다(트랙 E5-R2 후속).
	// 예전에는 이 등록이 OnDeserialized에만 있었다 — 그래서 DDOL 이송으로 씬을
	// 건넌 캔버스는 새 씬의 목록에 **영영 들어가지 않았다**. 실측: UITestScene의
	// 캔버스를 DDOL로 지정하고 씬을 바꾸면 오브젝트는 살아 넘어와 ImageComponent
	// 연결도 유지되는데(ui.status의 Image 1/1), 캔버스 목록은 0개였다. 그러면
	// UIManager::Update가 CurCanvas를 못 세우고 UI 내비게이션이 죽는다.
	// 바로 위 주석이 UITickSystem에 대해 적어 둔 사유와 같은 것인데, 캐시 쪽만
	// 그 결론을 못 받고 있었다.
	//
	// 이탈 쪽은 훅이 필요 없다 — 핸들이 씬을 떠나는 순간 해석되지 않고
	// (E5-R2의 fail-closed) UIManager::Update가 그 자리에서 걷어낸다.
	if (Entity* owner = GetOwner())
	{
		if (Scene* scene = owner->GetScene())
		{
			scene->AddCanvas(owner);
		}
	}
}

void Canvas::OnRemovingFromScene()
{
	UITickSystems->UnregisterCanvas(this);
	UIObjs.clear();
}

Entity* Canvas::GetFrontUIObject()
{
	// UI 자식이 하나도 없는 캔버스가 선택되는 순간 빈 벡터에 front()를 불러
	// __fastfail(0xC0000409)로 즉사했다. 런타임에 만든 캔버스는 항상 비어 있어서
	// component.add로 캔버스만 붙이고 재생하면 100% 재현됐다.
	Scene* scene = GetOwner() ? GetOwner()->GetScene() : nullptr;
	while (!UIObjs.empty())
	{
		if (Entity* object = scene ? scene->Resolve(UIObjs.front()) : nullptr)
			return object;
		UIObjs.erase(UIObjs.begin());
	}
	return nullptr;
}





void Canvas::OnDeserialized()
{
	// 캔버스 캐시 등록은 OnAddedToScene 하나로 모았다(위 주석) — 여기서 또 하면
	// 경로가 둘이 되고, DDOL 이송을 타지 않는 쪽이 정본처럼 보인다.
	prevCanvasName = CanvasName;
}

