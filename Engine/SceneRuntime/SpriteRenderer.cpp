#include "SpriteRenderer.h"
#include "DataSystem.h"
#include "Scene.h"
#include "RenderScene.h"
#include "SceneManager.h"
#include "BillboardType.h"

void SpriteRenderer::OnInitialized()
{
	auto scene = GetOwner()->m_ownerScene;
	auto renderScene = SceneManagers->GetRenderScene();
	if (scene)
	{
		scene->CollectSpriteRenderer(this);
		renderScene->RegisterCommand(this);
	}
}

void SpriteRenderer::OnAddedToScene()
{
	if (!HasLifecycleState(State_Initialized) || !GetOwner()) return;
	if (Scene* scene = GetOwner()->GetScene())
	{
		scene->CollectSpriteRenderer(this);
		if (auto* renderScene = SceneManagers->GetRenderScene())
			renderScene->RegisterCommand(this);
	}
}

void SpriteRenderer::OnRemovingFromScene()
{
	if (!GetOwner() || GetOwner()->IsDestroyMark()) return;
	if (Scene* scene = GetOwner()->GetScene())
	{
		scene->UnCollectSpriteRenderer(this);
		if (auto* renderScene = SceneManagers->GetRenderScene())
			renderScene->UnregisterCommand(this);
	}
}

void SpriteRenderer::OnUninitializing()
{
	auto scene = GetOwner()->m_ownerScene;
	auto renderScene = SceneManagers->GetRenderScene();
	if (scene)
	{
		scene->UnCollectSpriteRenderer(this);
		renderScene->UnregisterCommand(this);
	}
}

void SpriteRenderer::SetSprite(const std::shared_ptr<Texture>& ptr)
{
	m_Sprite = ptr;
	if (m_Sprite)
	{
		m_SpritePath = m_Sprite->m_name + m_Sprite->m_extension;
	}
	else
	{
		m_SpritePath.clear();
	}
	PublishRenderProxyDirty(ProxyDirty::Material);
}


void SpriteRenderer::OnDeserialized()
{
	// CT6-d: 구 ComponentFactory 분기 이동 — 동작·순서 보존.
	SetEnabled(true);
	if (m_SpritePath != "")
	{
		auto texture = DataSystems->LoadSharedTexture(m_SpritePath, DataSystem::TextureFileType::Texture);
		if (texture)
		{
			SetSprite(std::shared_ptr<Texture>(texture));
		}
	}
}

