#include "SpriteSheetComponent.h"
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
		scale = { worldRect.width / uiinfo.size.x,
				  worldRect.height / uiinfo.size.y };

		rect->SetSizeDelta(uiinfo.size);
	}
}

void SpriteSheetComponent::OnDestroy()
{
	auto scene = GetOwner()->m_ownerScene;
	auto renderScene = SceneManagers->GetRenderScene();
	if (scene)
	{
		renderScene->UnregisterCommand(this);
		UIManagers->UnregisterSpriteSheetComponent(this);
	}
}
