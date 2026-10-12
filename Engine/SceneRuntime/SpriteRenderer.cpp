#include "SpriteRenderer.h"
#include "DataSystem.h"
#include "Scene.h"
#include "RenderScene.h"
#include "SceneManager.h"
#include "BillboardType.h"
#include "../RenderEngine/AssetDepot/TextureAssetRuntime.h"

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

void SpriteRenderer::SetSprite(const own::shared_owner<const Texture>& ptr)
{
    std::string reference;
    if (ptr)
    {
        reference = !ptr->m_assetPath.empty() ? ptr->m_assetPath : ptr->m_name + ptr->m_extension;
        if (reference.empty())
        {
            if (const auto origin = ptr->GetAssetOrigin())
            {
                reference = DataSystems->GetFilePath(FileGuid(origin->resolved.entry.asset.key.assetId.value)).string();
                if (reference.empty())
                {
                    Debug::PrintLog(spdlog::level::err, "Cannot attach cooked SpriteRenderer texture without a serializable asset reference");
                    return;
                }
            }
        }
    }
    // Resolve before replacing the owner so a rejected cooked reference preserves it.
    m_Sprite = ptr;
    m_SpritePath = std::move(reference);
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
			SetSprite(texture);
		}
	}
}

