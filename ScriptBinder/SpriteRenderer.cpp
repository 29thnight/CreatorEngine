#include "SpriteRenderer.h"
#include "Scene.h"
#include "RenderScene.h"
#include "SceneManager.h"
#include "BillboardType.h"

void SpriteRenderer::Awake()
{
	auto scene = GetOwner()->m_ownerScene;
	auto renderScene = SceneManagers->GetRenderScene();
	if (scene)
	{
		scene->CollectSpriteRenderer(this);
		renderScene->RegisterCommand(this);
	}
}

void SpriteRenderer::OnDestroy()
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
		m_SpritePath = m_Sprite->m_name;
	}
	else
	{
		m_SpritePath.clear();
	}
}

void SpriteRenderer::DeserializeSprite(const std::shared_ptr<Texture>& ptr)
{
	m_Sprite = ptr;
	if (m_Sprite)
	{
		m_SpritePath = m_Sprite->m_name;
	}
	else
	{
		m_SpritePath.clear();
	}
}
