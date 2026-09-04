#include "SpriteSheetComponent.h"
#include "Canvas.h"
#include "DataSystem.h"
#include "SceneManager.h"
#include "RenderScene.h"
#include "Scene.h"
#include "RectTransformComponent.h"
#include "UIManager.h"
#include "UITickSystem.h"

void SpriteSheetComponent::LoadSpriteSheet(const file::path& path)
{
	m_spriteSheetPath = path.filename().string();
	m_spriteSheetTexture = DataSystems->LoadSharedTexture(m_spriteSheetPath,
		DataSystem::TextureFileType::SpriteSheet);
	uiinfo.size = m_spriteSheetTexture->GetImageSize();
	PublishRenderProxyDirty(ProxyDirty::Material | ProxyDirty::Payload);

	//origin = { uiinfo.size.x / 2, uiinfo.size.y / 2 };
}

void SpriteSheetComponent::OnInitialized()
{
	auto scene = GetOwner()->m_ownerScene;
	auto renderScene = SceneManagers->GetRenderScene();
	if (scene)
	{
		scene->CollectSpriteSheetComponent(this);
		if (renderScene) renderScene->RegisterCommand(this);
	}

	// 레지스트리 등록은 수명의 시작(Awake)에서 스스로 한다(6-1).
	// 캔버스 연결과 무관하게 등록되므로, 연결이 늦거나 없어도 유령이 되지 않는다.
	UIManagers->RegisterSpriteSheetComponent(this);
}

void SpriteSheetComponent::TickLayout(float tick)
{
	if (auto* rect = m_pOwner->GetComponent<RectTransformComponent>())
	{
		m_deltaTime = tick;
		const auto& worldRect = rect->GetWorldRect();
		pos = { worldRect.x + worldRect.width * 0.5f,
				worldRect.y + worldRect.height * 0.5f,
			0.0f };
		if (uiinfo.size.x != 0.f && uiinfo.size.y != 0.f)
		{
			scale = { worldRect.width / uiinfo.size.x,
						worldRect.height / uiinfo.size.y };
		}
		else
		{
			scale = { 1.f, 1.f };
		}
	}
}

void SpriteSheetComponent::OnUninitializing()
{
	auto scene = GetOwner()->m_ownerScene;
	auto renderScene = SceneManagers->GetRenderScene();
	if (scene)
	{
		scene->UnCollectSpriteSheetComponent(this);
		if (renderScene) renderScene->UnregisterCommand(this);
	}

	// 해제는 무조건 한다. 예전에는 씬이 널이면 건너뛰어 레지스트리에 dangling이 남았다.
	UIManagers->UnregisterSpriteSheetComponent(this);

	// 소속 캔버스 목록에서도 빠진다 — Canvas::Update의 매 프레임 청소를 대체한다(6-4).
	if (Canvas* owner = GetOwnerCanvas())
	{
		owner->RemoveUIObject(GetOwner());
	}
}

// 트랙 C3(레인 2: UI계) — UITickSystem 등록/해지. Awake/OnDestroy(컴포넌트당
// 1회 게이트)가 아니라 씬 편입/이탈 훅을 쓰는 이유는 UITickSystem.h 상단
// 주석 참조 — DDOL 오브젝트가 씬을 건널 때도 매번 다시 불려야 하기 때문이다.
// 실제 파괴 경로(PrefabUtility::ApplyComponentDiff·Scene::FlushPendingDestroy)
// 도 OnDestroy 직전에 OnRemovingFromScene을 먼저 부르므로, 이 시스템에서
// 빠지는 시점이 항상 실 파괴보다 먼저다.
void SpriteSheetComponent::OnAddedToScene()
{
	UIComponent::OnAddedToScene();
	UITickSystems->RegisterSpriteSheet(this);
	if (HasLifecycleState(State_Initialized) && GetOwner())
	{
		if (Scene* scene = GetOwner()->GetScene())
		{
			scene->CollectSpriteSheetComponent(this);
			if (auto* renderScene = SceneManagers->GetRenderScene())
				renderScene->RegisterCommand(this);
		}
	}
}

void SpriteSheetComponent::OnRemovingFromScene()
{
	UITickSystems->UnregisterSpriteSheet(this);
	if (GetOwner() && !GetOwner()->IsDestroyMark())
	{
		if (Scene* scene = GetOwner()->GetScene())
		{
			scene->UnCollectSpriteSheetComponent(this);
			if (auto* renderScene = SceneManagers->GetRenderScene())
				renderScene->UnregisterCommand(this);
		}
	}
	UIComponent::OnRemovingFromScene();
}



void SpriteSheetComponent::OnDeserialized()
{
	// CT6-d: 구 ComponentFactory 분기 이동 — 저장값과 무관한 프리뷰 해제 유지.
	m_isPreview = false;
	if (!m_spriteSheetPath.empty())
	{
		LoadSpriteSheet(m_spriteSheetPath);
	}
	else
	{
		Debug->LogError("SpriteSheetComponent is missing m_spriteSheetPath");
	}
}

