#include "SpriteSheetComponent.h"
#include "Canvas.h"
#include "DataSystem.h"
#include "SceneManager.h"
#include "RenderScene.h"
#include "Scene.h"
#include "RectTransformComponent.h"
#include "UIManager.h"

void SpriteSheetComponent::LoadSpriteSheet(const file::path& path)
{
	m_spriteSheetPath = path.filename().string();
	m_spriteSheetTexture = DataSystems->LoadSharedTexture(m_spriteSheetPath,
		DataSystem::TextureFileType::SpriteSheet);
	uiinfo.size = m_spriteSheetTexture->GetImageSize();

	//origin = { uiinfo.size.x / 2, uiinfo.size.y / 2 };
}

void SpriteSheetComponent::Awake()
{
	auto scene = GetOwner()->m_ownerScene;
	auto renderScene = SceneManagers->GetRenderScene();
	if (scene)
	{
		renderScene->RegisterCommand(this);
	}

	// 레지스트리 등록은 수명의 시작(Awake)에서 스스로 한다(6-1).
	// 캔버스 연결과 무관하게 등록되므로, 연결이 늦거나 없어도 유령이 되지 않는다.
	UIManagers->RegisterSpriteSheetComponent(this);
}

void SpriteSheetComponent::Update(float tick)
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

void SpriteSheetComponent::OnDestroy()
{
	auto scene = GetOwner()->m_ownerScene;
	auto renderScene = SceneManagers->GetRenderScene();
	if (scene)
	{
		renderScene->UnregisterCommand(this);
	}

	// 해제는 무조건 한다. 예전에는 씬이 널이면 건너뛰어 레지스트리에 dangling이 남았다.
	UIManagers->UnregisterSpriteSheetComponent(this);

	// 소속 캔버스 목록에서도 빠진다 — Canvas::Update의 매 프레임 청소를 대체한다(6-4).
	if (Canvas* owner = GetOwnerCanvas())
	{
		owner->RemoveUIObject(GetOwner());
	}
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

