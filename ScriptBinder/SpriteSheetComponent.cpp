#include "SpriteSheetComponent.h"
#include "DataSystem.h"
#include "SceneManager.h"
#include "RenderScene.h"
#include "Scene.h"
#include "RectTransformComponent.h"
#include "UIManager.h"

void SpriteSheetComponent::LoadSpriteSheet(std::string_view path)
{
	m_spriteSheetPath = path.data();
	m_spriteSheetTexture = DataSystems->LoadSharedTexture(path, 
		DataSystem::TextureFileType::SpriteSheet);
}

void SpriteSheetComponent::Awake()
{
	//auto scene = GetOwner()->m_ownerScene;
	//auto renderScene = SceneManagers->GetRenderScene();
	//if (scene)
	//{
	//	renderScene->RegisterCommand(this);
	//}
}

void SpriteSheetComponent::Update(float tick)
{
	if (auto* rect = m_pOwner->GetComponent<RectTransformComponent>())
	{
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
	//auto scene = GetOwner()->m_ownerScene;
	//auto renderScene = SceneManagers->GetRenderScene();
	//if (scene)
	//{
	//	renderScene->UnregisterCommand(this);
	//	UIManagers->UnregisterSpriteSheetComponent(this);
	//}
}
