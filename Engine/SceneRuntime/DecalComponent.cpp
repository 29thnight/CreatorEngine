#include "DecalComponent.h"
#include "DecalSystem.h"
#include "Texture.h"
#include "SceneManager.h"
#include "RenderScene.h"
#include "Scene.h"

void DecalComponent::OnInitialized()
{
    auto scene = GetOwner()->m_ownerScene;
    auto renderScene = SceneManagers->GetRenderScene();
    if (scene)
    {
        scene->CollectDecalComponent(this);
        if (renderScene)
            renderScene->RegisterCommand(this);
    }

	SetDecalTexture(m_diffusefileName.c_str());
    SetNormalTexture(m_normalFileName.c_str());
    SetORMTexture(m_ormFileName.c_str());
}

// 트랙 C3 — DecalSystem 등록/해지. Awake/OnDestroy(컴포넌트당 1회 게이트)가
// 아니라 씬 편입/이탈 훅을 쓰는 이유는 AnimatorSystem.h 상단 주석 참고 — DDOL
// 오브젝트가 씬을 건널 때도 매번 다시 불려야 하기 때문이다. 실제 파괴 경로
// (Scene::FlushPendingDestroy·PrefabUtility::ApplyComponentDiff)도
// OnUninitializing(위 OnDestroy 브리지) 직전에 OnRemovingFromScene을 먼저
// 부르므로, 이 시스템에서 빠지는 시점이 항상 실 파괴보다 먼저다.
void DecalComponent::OnAddedToScene()
{
    DecalSystems->Register(this);
	if (HasLifecycleState(State_Initialized) && GetOwner())
	{
		if (Scene* scene = GetOwner()->GetScene())
		{
			scene->CollectDecalComponent(this);
			if (auto* renderScene = SceneManagers->GetRenderScene())
				renderScene->RegisterCommand(this);
		}
	}
}

void DecalComponent::OnRemovingFromScene()
{
    DecalSystems->Unregister(this);
	if (GetOwner() && !GetOwner()->IsDestroyMark())
	{
		if (Scene* scene = GetOwner()->GetScene())
		{
			scene->UnCollectDecalComponent(this);
			if (auto* renderScene = SceneManagers->GetRenderScene())
				renderScene->UnregisterCommand(this);
		}
	}
}

void DecalComponent::OnUninitializing()
{
    auto scene = GetOwner()->m_ownerScene;
    auto renderScene = SceneManagers->GetRenderScene();
    if (scene)
    {
        scene->UnCollectDecalComponent(this);
        if(renderScene)
            renderScene->UnregisterCommand(this);
    }
}

void DecalComponent::SetDecalTexture(const std::string_view& fileName)
{
	file::path filename = fileName;
	file::path filepath = PathFinder::Relative("Textures\\") / filename.filename();
	m_decalTextureOwner = Texture::LoadSharedFromPath(filepath.string());
	m_decalTexture = m_decalTextureOwner.get();
    m_diffusefileName = fileName;
	PublishRenderProxyDirty(ProxyDirty::Material);
}

void DecalComponent::SetDecalTexture(const FileGuid& fileGuid)
{
}

void DecalComponent::SetNormalTexture(const std::string_view& fileName)
{
    file::path filename = fileName;
    file::path filepath = PathFinder::Relative("Textures\\") / filename.filename();
	m_normalTextureOwner = Texture::LoadSharedFromPath(filepath.string());
	m_normalTexture = m_normalTextureOwner.get();
    m_normalFileName = fileName;
	PublishRenderProxyDirty(ProxyDirty::Material);
}

void DecalComponent::SetNormalTexture(const FileGuid& fileGuid)
{
}

void DecalComponent::SetORMTexture(const std::string_view& fileName)
{
    file::path filename = fileName;
    file::path filepath = PathFinder::Relative("Textures\\") / filename.filename();
	m_ormTextureOwner = Texture::LoadSharedFromPath(filepath.string());
	m_occluroughmetalTexture = m_ormTextureOwner.get();
    m_ormFileName = fileName;
	PublishRenderProxyDirty(ProxyDirty::Material);
}

void DecalComponent::SetORMTexture(const FileGuid& fileGuid)
{
}
