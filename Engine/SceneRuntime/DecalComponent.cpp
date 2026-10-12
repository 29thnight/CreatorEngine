#include "DecalComponent.h"
#include "DecalSystem.h"
#include "Texture.h"
#include "SceneManager.h"
#include "RenderScene.h"
#include "Scene.h"
#include "DataSystem.h"
#include <algorithm>

void DecalComponent::OnInitialized()
{
    auto scene = GetOwner()->m_ownerScene;
    auto renderScene = SceneManagers->GetRenderScene();
    if (scene)
    {
        scene->CollectDecalComponent(this);
        if (renderScene)
        {
            renderScene->RegisterCommand(this);
        }
    }

	SetDecalTexture(m_diffusefileName.c_str());
    SetNormalTexture(m_normalFileName.c_str());
    SetORMTexture(m_ormFileName.c_str());
}

// 트랙 C3 — DecalSystem 등록/해지. OnInitialized/OnUninitializing(컴포넌트당 1회 게이트)이
// 아니라 씬 편입/이탈 훅을 쓰는 이유는 AnimatorSystem.h 상단 주석 참고 — DDOL
// 오브젝트가 씬을 건널 때도 매번 다시 불려야 하기 때문이다. 실제 파괴 경로
// (Scene::FlushPendingDestroy·PrefabUtility::ApplyComponentDiff)도
// OnUninitializing 직전에 OnRemovingFromScene을 먼저
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
			{
				renderScene->RegisterCommand(this);
			}
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
			{
				renderScene->UnregisterCommand(this);
			}
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
        {
            renderScene->UnregisterCommand(this);
        }
    }
    ReleaseManagedResources();
}

void DecalComponent::SetDecalTexture(const std::string_view& fileName)
{
    m_diffusefileName = fileName;
    RequestTexture(m_diffusefileName, 0u, m_decalTextureOwner, m_decalTexture);
    PollTextureRequests();
}

void DecalComponent::SetDecalTexture(const FileGuid& fileGuid)
{
    const auto reference = fileGuid == FileGuid{} ? std::string{} : Uuid::ToString(fileGuid.m_guid);
    SetDecalTexture(std::string_view(reference));
}

void DecalComponent::SetNormalTexture(const std::string_view& fileName)
{
    m_normalFileName = fileName;
    RequestTexture(m_normalFileName, 1u, m_normalTextureOwner, m_normalTexture);
    PollTextureRequests();
}

void DecalComponent::SetNormalTexture(const FileGuid& fileGuid)
{
    const auto reference = fileGuid == FileGuid{} ? std::string{} : Uuid::ToString(fileGuid.m_guid);
    SetNormalTexture(std::string_view(reference));
}

void DecalComponent::SetORMTexture(const std::string_view& fileName)
{
    m_ormFileName = fileName;
    RequestTexture(m_ormFileName, 2u, m_ormTextureOwner, m_occluroughmetalTexture);
    PollTextureRequests();
}

void DecalComponent::SetORMTexture(const FileGuid& fileGuid)
{
    const auto reference = fileGuid == FileGuid{} ? std::string{} : Uuid::ToString(fileGuid.m_guid);
    SetORMTexture(std::string_view(reference));
}

void DecalComponent::RequestTexture(std::string_view reference, std::size_t slot,
    own::shared_owner<const Texture>& owner, const Texture*& alias)
{
    auto& request = m_textureRequests[slot];
    request.Cancel();
    request = {};
    m_texturePending[slot] = false;
    m_textureRetryOnRevision[slot] = false;
    if (reference.empty())
    {
        owner.reset();
        alias = nullptr;
        PublishRenderProxyDirty(ProxyDirty::Material);
        return;
    }

    const auto catalog = DataSystems->GetCookedCatalog();
    m_textureResolverRevisions[slot] = catalog ? catalog->ResolverRevision() : 0u;
    FileGuid guid;
    const bool logicalIdentity = Uuid::TryParse(reference, guid.m_guid);
    if (logicalIdentity && guid == FileGuid{})
    {
        owner.reset();
        alias = nullptr;
        PublishRenderProxyDirty(ProxyDirty::Material);
        return;
    }
    if (!logicalIdentity)
    {
        // Compatibility is a registry lookup, never a source file load. Offline
        // scene cooking lowers these filename fields to canonical logical IDs.
        const auto path = PathFinder::Relative("Textures\\") / file::path(reference).filename();
        guid = DataSystems->GetFileGuid(path.lexically_normal());
    }
    const AssetDepot::AssetLink<Texture> link{ { experiment::AssetId{ guid.m_guid }, {} } };
    if (!link.IsValid())
    {
        m_textureRetryOnRevision[slot] = PathFinder::IsAssetAuthoringEnabled();
        Debug::PrintLog(spdlog::level::err, "Decal texture has no registered identity: " + std::string(reference));
        return;
    }
    AssetDepot::TextureAssetVariant variant;
    variant.role = static_cast<std::uint32_t>(DataSystem::TextureFileType::Texture) + 1u;
    // Data channels cannot inherit sRGB transfer from source metadata. A locked
    // incompatible import fails in typed acquisition rather than being relabeled.
    if (slot != 0u)
    {
        variant.colorSpace = AssetDepot::TextureAssetColorSpace::Linear;
    }
    if (auto prepared = DataSystems->TryAcquire<Texture>(link, variant))
    {
        owner = std::move(prepared);
        alias = &*owner;
        PublishRenderProxyDirty(ProxyDirty::Material);
        return;
    }
    request = DataSystems->RequestAsync<Texture>(link, variant);
    m_texturePending[slot] = true;
}

void DecalComponent::ReplaceTextureOwner(const Texture* previous,
    const own::shared_owner<const Texture>& replacement)
{
    std::array<own::shared_owner<const Texture>*, 3> owners{
        &m_decalTextureOwner, &m_normalTextureOwner, &m_ormTextureOwner };
    std::array<const Texture**, 3> aliases{ &m_decalTexture, &m_normalTexture, &m_occluroughmetalTexture };
    for (std::size_t slot = 0; slot < owners.size(); ++slot)
    {
        if (*owners[slot] && &**owners[slot] == previous)
        {
            *owners[slot] = replacement;
            *aliases[slot] = &*replacement;
            m_texturePending[slot] = false;
            m_textureRequests[slot] = {};
            PublishRenderProxyDirty(ProxyDirty::Material);
        }
    }
}

void DecalComponent::PollTextureRequests()
{
    const auto active = [](bool value) { return value; };
    const bool retryOnRevision = std::ranges::any_of(m_textureRetryOnRevision, active);
    if (!retryOnRevision && !std::ranges::any_of(m_texturePending, active))
    {
        return;
    }
    std::array<own::shared_owner<const Texture>*, 3> owners{
        &m_decalTextureOwner, &m_normalTextureOwner, &m_ormTextureOwner };
    std::array<const Texture**, 3> aliases{ &m_decalTexture, &m_normalTexture, &m_occluroughmetalTexture };
    std::array<const std::string*, 3> references{ &m_diffusefileName, &m_normalFileName, &m_ormFileName };
    std::uint64_t resolverRevision{};
    if (retryOnRevision)
    {
        const auto catalog = DataSystems->GetCookedCatalog();
        resolverRevision = catalog ? catalog->ResolverRevision() : 0u;
    }
    bool changed = false;
    for (std::size_t slot = 0u; slot < m_textureRequests.size(); ++slot)
    {
        if (!m_texturePending[slot])
        {
            if (m_textureRetryOnRevision[slot] && resolverRevision != m_textureResolverRevisions[slot])
            {
                // Initial authoring import may publish its first cooked mount
                // after the component is initialized. Do not retry each frame.
                RequestTexture(*references[slot], slot, *owners[slot], *aliases[slot]);
            }
            continue;
        }
        const auto result = m_textureRequests[slot].Snapshot();
        if (result.status == AssetDepot::AssetRequestStatus::Pending)
        {
            continue;
        }
        m_texturePending[slot] = false;
        m_textureRequests[slot] = {};
        if (result.status == AssetDepot::AssetRequestStatus::Stale)
        {
            // An unrelated mount may advance the resolver while this logical
            // request is pending. Retry once at the next owner publication,
            // without recursion, waiting, or dropping the accepted generation.
            RequestTexture(*references[slot], slot, *owners[slot], *aliases[slot]);
            continue;
        }
        if (result.status == AssetDepot::AssetRequestStatus::Ready && result.asset)
        {
            *owners[slot] = result.asset;
            *aliases[slot] = &*result.asset;
            changed = true;
        }
        else
        {
            m_textureRetryOnRevision[slot] = result.status == AssetDepot::AssetRequestStatus::Failed;
            Debug::PrintLog(spdlog::level::err, "Decal texture preparation failed; keeping its previous texture: "
                + result.message);
        }
    }
    if (changed)
    {
        PublishRenderProxyDirty(ProxyDirty::Material);
    }
}

void DecalComponent::ReleaseManagedResources()
{
    for (auto& request : m_textureRequests)
    {
        request.Cancel();
        request = {};
    }
    m_texturePending.fill(false);
    m_textureRetryOnRevision.fill(false);
    m_textureResolverRevisions.fill(0u);
    m_decalTexture = nullptr;
    m_normalTexture = nullptr;
    m_occluroughmetalTexture = nullptr;
    m_decalTextureOwner.reset();
    m_normalTextureOwner.reset();
    m_ormTextureOwner.reset();
}
