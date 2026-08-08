// RenderScene의 컴포넌트 접점 구현 (PHASE 4-2 C1).
//
// Register/Update/Unregister/InvaildCheck/MakeProxyCommand 계열은 게임플레이
// 컴포넌트를 읽어 프록시를 만들거나 갱신하는 경계 코드다. 렌더 측 로직
// (프레임 갱신, 스냅샷, 패스 데이터)은 RenderEngine/RenderScene.cpp에 남는다.
#include "RenderScene.h"
#include "Animator.h"
#include "MeshRenderer.h"
#include "FoliageComponent.h"
#include "Terrain.h"
#include "DecalComponent.h"
#include "ImageComponent.h"
#include "TextComponent.h"
#include "SpriteRenderer.h"
#include "SpriteSheetComponent.h"
#include "LightComponent.h"
#include "PrimitiveRenderProxy.h"
#include "LightRenderProxy.h"
// Skeleton::MAX_BONES를 직접 쓴다. 예전에는 다른 헤더를 타고 딸려
// 들어왔는데, include를 정리하면서 그 경로가 끊겼다 — AvatarMask.h의
// 전방 선언만 남아 비유니티 빌드에서 드러났다.
#include "Skeleton.h"

void RenderScene::RegisterAnimator(const std::shared_ptr<Animator>& animatorPtr)
{
	if (nullptr == animatorPtr) return;

	HashedGuid animatorGuid = animatorPtr->GetInstanceID();

	// 애니메이터/팔레트 맵은 프록시 맵과 같은 락으로 보호한다.
	// ProxyCommand 생성자가 스레드풀에서 이 맵들을 동시에 읽기 때문이다.
	SpinLock lock(m_proxyMapFlag);

	if (m_animatorMap.find(animatorGuid) != m_animatorMap.end()) return;

	m_animatorMap[animatorGuid] = animatorPtr;

	m_palleteMap[animatorGuid].second = std::make_shared<Mathf::xMatrix[]>(Skeleton::MAX_BONES);
}

void RenderScene::UnregisterAnimator(const std::shared_ptr<Animator>& animatorPtr)
{
	if (nullptr == animatorPtr) return;

	HashedGuid animatorGuid = animatorPtr->GetInstanceID();

	SpinLock lock(m_proxyMapFlag);

	m_animatorMap.erase(animatorGuid);

	// 맵에서 지워도 버퍼를 참조 중인 프록시가 있으면 shared_ptr가 수명을 유지한다.
	// 마지막 참조가 사라질 때 해제되므로 렌더 스레드가 해제된 메모리를 읽는 일이 없다.
	m_palleteMap.erase(animatorGuid);
}

void RenderScene::RegisterCommand(MeshRenderer* meshRendererPtr)
{
	if (nullptr == meshRendererPtr) return;

	HashedGuid meshRendererGuid = meshRendererPtr->GetInstanceID();

	SpinLock lock(m_proxyMapFlag);

	if (m_proxyMap.find(meshRendererGuid) != m_proxyMap.end()) return;

	// Create a new proxy for the mesh renderer and insert it into the map
	auto managedCommand = std::make_shared<MeshRenderProxy>(meshRendererPtr);
	m_proxyMap[meshRendererGuid] = managedCommand;
}

void RenderScene::RegisterCommand(FoliageComponent* foliagePtr)
{
    if (nullptr == foliagePtr) return;

    HashedGuid guid = foliagePtr->GetInstanceID();

    SpinLock lock(m_proxyMapFlag);

    if (m_proxyMap.find(guid) != m_proxyMap.end()) return;

    auto managed = std::make_shared<FoliageRenderProxy>(foliagePtr);
    m_proxyMap[guid] = managed;
}

bool RenderScene::InvaildCheckMeshRenderer(MeshRenderer* meshRendererPtr)
{
	if (nullptr == meshRendererPtr || meshRendererPtr->IsDestroyMark()) return false;

	auto owner = meshRendererPtr->GetOwner();
	if (nullptr == owner || owner->IsDestroyMark()) return false;

	HashedGuid meshRendererGuid = meshRendererPtr->GetInstanceID();

	SpinLock lock(m_proxyMapFlag);

	if (m_proxyMap.find(meshRendererGuid) == m_proxyMap.end()) return false;

	auto& proxyObject = m_proxyMap[meshRendererGuid];

	if (nullptr == proxyObject) return false;

	return true;
}

bool RenderScene::InvaildCheckFoliage(FoliageComponent* foliagePtr)
{
    if (nullptr == foliagePtr || foliagePtr->IsDestroyMark()) return false;

    auto owner = foliagePtr->GetOwner();
    if (nullptr == owner || owner->IsDestroyMark()) return false;

    HashedGuid guid = foliagePtr->GetInstanceID();

    SpinLock lock(m_proxyMapFlag);

    if (m_proxyMap.find(guid) == m_proxyMap.end()) return false;

    auto& proxyObject = m_proxyMap[guid];

    if (nullptr == proxyObject) return false;

    return true;
}


void RenderScene::UpdateCommand(MeshRenderer* meshRendererPtr)
{
    ProxyCommand moveCommand = MakeProxyCommand(meshRendererPtr);
    ProxyCommandQueue->PushProxyCommand(std::move(moveCommand));
}

void RenderScene::UpdateCommand(FoliageComponent* foliagePtr)
{
    ProxyCommand moveCommand = MakeProxyCommand(foliagePtr);
    ProxyCommandQueue->PushProxyCommand(std::move(moveCommand));
}

ProxyCommand RenderScene::MakeProxyCommand(MeshRenderer* meshRendererPtr)
{
	if (!InvaildCheckMeshRenderer(meshRendererPtr)) 
	{
		throw std::runtime_error("InvaildCheckMeshRenderer");
	}

    ProxyCommand command(meshRendererPtr);
    return command;
}

ProxyCommand RenderScene::MakeProxyCommand(FoliageComponent* foliagePtr)
{
    if (!InvaildCheckFoliage(foliagePtr))
    {
		throw std::runtime_error("InvaildCheckFoliage");
    }

    ProxyCommand command(foliagePtr);
    return command;
}

void RenderScene::UnregisterCommand(MeshRenderer* meshRendererPtr)
{
	if (nullptr == meshRendererPtr) return;

	HashedGuid meshRendererGuid = meshRendererPtr->GetInstanceID();

	SpinLock lock(m_proxyMapFlag);
	
	if (m_proxyMap.find(meshRendererGuid) == m_proxyMap.end()) return;

        m_proxyMap[meshRendererGuid]->DestroyProxy();
}

void RenderScene::RegisterCommand(TerrainComponent* terrainPtr)
{
	if (nullptr == terrainPtr) return;
	HashedGuid terrainGuid = terrainPtr->GetInstanceID();
	SpinLock lock(m_proxyMapFlag);
	if (m_proxyMap.find(terrainGuid) != m_proxyMap.end()) return;
	// Create a new proxy for the terrain and insert it into the map
	auto managedCommand = std::make_shared<TerrainRenderProxy>(terrainPtr);
	m_proxyMap[terrainGuid] = managedCommand;
}

bool RenderScene::InvaildCheckTerrain(TerrainComponent* terrainPtr)
{
	if (nullptr == terrainPtr || terrainPtr->IsDestroyMark()) return false;
	auto owner = terrainPtr->GetOwner();
	if (nullptr == owner || owner->IsDestroyMark()) return false;
	HashedGuid terrainGuid = terrainPtr->GetInstanceID();
	SpinLock lock(m_proxyMapFlag);
	if (m_proxyMap.find(terrainGuid) == m_proxyMap.end()) return false;
	auto& proxyObject = m_proxyMap[terrainGuid];
	if (nullptr == proxyObject) return false;
	return true;
}

void RenderScene::UpdateCommand(TerrainComponent* terrainPtr)
{
	if (!InvaildCheckTerrain(terrainPtr)) 
	{
		throw std::runtime_error("InvaildCheckTerrain");
	}
	ProxyCommand moveCommand = MakeProxyCommand(terrainPtr);
	ProxyCommandQueue->PushProxyCommand(std::move(moveCommand));
}

ProxyCommand RenderScene::MakeProxyCommand(TerrainComponent* terrainPtr)
{
	if (!InvaildCheckTerrain(terrainPtr)) 
	{
		throw std::runtime_error("InvaildCheckTerrain");
	}
	ProxyCommand command(terrainPtr);
	return command;
}

void RenderScene::UnregisterCommand(TerrainComponent* terrainPtr)
{
	if (nullptr == terrainPtr) return;
	HashedGuid terrainGuid = terrainPtr->GetInstanceID();
	SpinLock lock(m_proxyMapFlag);

	if (m_proxyMap.find(terrainGuid) == m_proxyMap.end()) return;
	m_proxyMap[terrainGuid]->DestroyProxy();
}

void RenderScene::UnregisterCommand(FoliageComponent* foliagePtr)
{
    if (nullptr == foliagePtr) return;

    HashedGuid guid = foliagePtr->GetInstanceID();

    SpinLock lock(m_proxyMapFlag);

    if (m_proxyMap.find(guid) == m_proxyMap.end()) return;

    m_proxyMap[guid]->DestroyProxy();
}

void RenderScene::RegisterCommand(DecalComponent* decalPtr)
{
	if (nullptr == decalPtr) return;

	HashedGuid guid = decalPtr->GetInstanceID();

	SpinLock lock(m_proxyMapFlag);

	if (m_proxyMap.find(guid) != m_proxyMap.end()) return;

	auto managed = std::make_shared<DecalRenderProxy>(decalPtr);
	m_proxyMap[guid] = managed;
}

bool RenderScene::InvaildCheckDecal(DecalComponent* decalPtr)
{
	if (nullptr == decalPtr || decalPtr->IsDestroyMark()) return false;

	auto owner = decalPtr->GetOwner();
	if (nullptr == owner || owner->IsDestroyMark()) return false;

	HashedGuid guid = decalPtr->GetInstanceID();

	SpinLock lock(m_proxyMapFlag);

	if (m_proxyMap.find(guid) == m_proxyMap.end()) return false;

	auto& proxyObject = m_proxyMap[guid];

	if (nullptr == proxyObject) return false;

	return true;
}

void RenderScene::UpdateCommand(DecalComponent* decalPtr)
{
	ProxyCommand moveCommand = MakeProxyCommand(decalPtr);
	ProxyCommandQueue->PushProxyCommand(std::move(moveCommand));
}

ProxyCommand RenderScene::MakeProxyCommand(DecalComponent* decalPtr)
{
	if (!InvaildCheckDecal(decalPtr))
	{
		throw std::runtime_error("InvaildCheckDecal");
	}

	ProxyCommand command(decalPtr);
	return command;
}

void RenderScene::UnregisterCommand(DecalComponent* decalPtr)
{
	if (nullptr == decalPtr) return;

	HashedGuid guid = decalPtr->GetInstanceID();

	SpinLock lock(m_proxyMapFlag);

	if (m_proxyMap.find(guid) == m_proxyMap.end()) return;

	m_proxyMap[guid]->DestroyProxy();
}

void RenderScene::RegisterCommand(ImageComponent* imagePtr)
{
	if (nullptr == imagePtr) return;
	HashedGuid imageGuid = imagePtr->GetInstanceID();
	SpinLock lock(m_uiProxyMapFlag);
	if (m_uiProxyMap.find(imageGuid) != m_uiProxyMap.end()) return;
	// Create a new proxy for the image and insert it into the map
	auto managedCommand = std::make_shared<UIRenderProxy>(imagePtr);
	m_uiProxyMap[imageGuid] = managedCommand;
}

bool RenderScene::InvaildCheckImage(ImageComponent* imagePtr)
{
	if (nullptr == imagePtr || imagePtr->IsDestroyMark()) return false;
	auto owner = imagePtr->GetOwner();
	if (nullptr == owner || owner->IsDestroyMark()) return false;
	HashedGuid imageGuid = imagePtr->GetInstanceID();
	SpinLock lock(m_uiProxyMapFlag);
	if (m_uiProxyMap.find(imageGuid) == m_uiProxyMap.end()) return false;
	auto& proxyObject = m_uiProxyMap[imageGuid];
	if (nullptr == proxyObject) return false;
	return true;
}

void RenderScene::UpdateCommand(ImageComponent* imagePtr)
{
	if (!InvaildCheckImage(imagePtr)) 
	{
		return;
		//throw std::runtime_error("InvaildCheckImage");
	}
	ProxyCommand moveCommand = MakeProxyCommand(imagePtr);
	ProxyCommandQueue->PushProxyCommand(std::move(moveCommand));
}

ProxyCommand RenderScene::MakeProxyCommand(ImageComponent* imagePtr)
{
	if (!InvaildCheckImage(imagePtr)) 
	{
		throw std::runtime_error("InvaildCheckImage");
	}
	ProxyCommand command(imagePtr);
	return command;
}

void RenderScene::UnregisterCommand(ImageComponent* imagePtr)
{
	if (nullptr == imagePtr) return;
	HashedGuid imageGuid = imagePtr->GetInstanceID();
	SpinLock lock(m_uiProxyMapFlag);
	if (m_uiProxyMap.find(imageGuid) == m_uiProxyMap.end()) return;
	m_uiProxyMap[imageGuid]->DestroyProxy();
}

void RenderScene::RegisterCommand(TextComponent* textPtr)
{
	if (nullptr == textPtr) return;
	HashedGuid textGuid = textPtr->GetInstanceID();
	SpinLock lock(m_uiProxyMapFlag);
	if (m_uiProxyMap.find(textGuid) != m_uiProxyMap.end()) return;
	// Create a new proxy for the text and insert it into the map
	auto managedCommand = std::make_shared<UIRenderProxy>(textPtr);
	m_uiProxyMap[textGuid] = managedCommand;
}

bool RenderScene::InvaildCheckText(TextComponent* textPtr)
{
	if (nullptr == textPtr || textPtr->IsDestroyMark()) return false;
	auto owner = textPtr->GetOwner();
	if (nullptr == owner || owner->IsDestroyMark()) return false;
	HashedGuid textGuid = textPtr->GetInstanceID();
	SpinLock lock(m_uiProxyMapFlag);
	if (m_uiProxyMap.find(textGuid) == m_uiProxyMap.end()) return false;
	auto& proxyObject = m_uiProxyMap[textGuid];
	if (nullptr == proxyObject) return false;
	return true;
}

void RenderScene::UpdateCommand(TextComponent* textPtr)
{
	if (!InvaildCheckText(textPtr)) 
	{
		return;
		//throw std::runtime_error("InvaildCheckText");
	}
	ProxyCommand moveCommand = MakeProxyCommand(textPtr);
	ProxyCommandQueue->PushProxyCommand(std::move(moveCommand));
}

ProxyCommand RenderScene::MakeProxyCommand(TextComponent* textPtr)
{
	if (!InvaildCheckText(textPtr)) 
	{
		throw std::runtime_error("InvaildCheckText");
	}
	ProxyCommand command(textPtr);
	return command;
}

void RenderScene::UnregisterCommand(TextComponent* textPtr)
{
	if (nullptr == textPtr) return;
	HashedGuid textGuid = textPtr->GetInstanceID();
	SpinLock lock(m_uiProxyMapFlag);
	if (m_uiProxyMap.find(textGuid) == m_uiProxyMap.end()) return;
	m_uiProxyMap[textGuid]->DestroyProxy();
}

void RenderScene::RegisterCommand(SpriteSheetComponent* spriteSheetPtr)
{
	if (nullptr == spriteSheetPtr) return;
	HashedGuid guid = spriteSheetPtr->GetInstanceID();
	SpinLock lock(m_uiProxyMapFlag);
	if (m_uiProxyMap.find(guid) != m_uiProxyMap.end()) return;
	auto managed = std::make_shared<UIRenderProxy>(spriteSheetPtr);
	m_uiProxyMap[guid] = managed;
}

void RenderScene::UnregisterCommand(SpriteSheetComponent* spriteSheetPtr)
{
	if (nullptr == spriteSheetPtr) return;
	HashedGuid guid = spriteSheetPtr->GetInstanceID();
	SpinLock lock(m_uiProxyMapFlag);
	if (m_uiProxyMap.find(guid) == m_uiProxyMap.end()) return;
	m_uiProxyMap[guid]->DestroyProxy();
}

void RenderScene::UpdateCommand(SpriteSheetComponent* spriteSheetPtr)
{
	if (!InvaildCheckSpriteSheet(spriteSheetPtr)) 
	{
		throw std::runtime_error("InvaildCheckSpriteSheet");
	}
	ProxyCommand moveCommand = MakeProxyCommand(spriteSheetPtr);
	ProxyCommandQueue->PushProxyCommand(std::move(moveCommand));
}

ProxyCommand RenderScene::MakeProxyCommand(SpriteSheetComponent* spriteSheetPtr)
{
	if (!InvaildCheckSpriteSheet(spriteSheetPtr)) 
	{
		throw std::runtime_error("InvaildCheckSpriteSheet");
	}
	ProxyCommand command(spriteSheetPtr);
	return command;
}

bool RenderScene::InvaildCheckSpriteSheet(SpriteSheetComponent* spriteSheetPtr)
{
	if (nullptr == spriteSheetPtr || spriteSheetPtr->IsDestroyMark()) return false;
	auto owner = spriteSheetPtr->GetOwner();
	if (nullptr == owner || owner->IsDestroyMark()) return false;
	HashedGuid guid = spriteSheetPtr->GetInstanceID();
	SpinLock lock(m_uiProxyMapFlag);
	if (m_uiProxyMap.find(guid) == m_uiProxyMap.end()) return false;
	auto& proxyObject = m_uiProxyMap[guid];
	if (nullptr == proxyObject) return false;
	return true;
}

void RenderScene::RegisterCommand(SpriteRenderer* spriteRendererPtr)
{
	if (nullptr == spriteRendererPtr) return;
	HashedGuid spriteRendererGuid = spriteRendererPtr->GetInstanceID();
	SpinLock lock(m_proxyMapFlag);
	if (m_proxyMap.find(spriteRendererGuid) != m_proxyMap.end()) return;
	// Create a new proxy for the sprite renderer and insert it into the map
	auto managedCommand = std::make_shared<SpriteRenderProxy>(spriteRendererPtr);
	m_proxyMap[spriteRendererGuid] = managedCommand;
}

bool RenderScene::InvaildCheckSpriteRenderer(SpriteRenderer* spriteRendererPtr)
{
	if (nullptr == spriteRendererPtr || spriteRendererPtr->IsDestroyMark()) return false;
	auto owner = spriteRendererPtr->GetOwner();
	if (nullptr == owner || owner->IsDestroyMark()) return false;
	HashedGuid spriteRendererGuid = spriteRendererPtr->GetInstanceID();
	SpinLock lock(m_proxyMapFlag);
	if (m_proxyMap.find(spriteRendererGuid) == m_proxyMap.end()) return false;
	auto& proxyObject = m_proxyMap[spriteRendererGuid];
	if (nullptr == proxyObject) return false;
	return true;
}

void RenderScene::UpdateCommand(SpriteRenderer* spriteRendererPtr)
{
	if (!InvaildCheckSpriteRenderer(spriteRendererPtr)) 
	{
		throw std::runtime_error("InvaildCheckSpriteRenderer");
	}
	ProxyCommand moveCommand = MakeProxyCommand(spriteRendererPtr);
	ProxyCommandQueue->PushProxyCommand(std::move(moveCommand));
}

ProxyCommand RenderScene::MakeProxyCommand(SpriteRenderer* spriteRendererPtr)
{
	if (!InvaildCheckSpriteRenderer(spriteRendererPtr)) 
	{
		throw std::runtime_error("InvaildCheckSpriteRenderer");
	}
	ProxyCommand command(spriteRendererPtr);
	return command;
}

void RenderScene::UnregisterCommand(SpriteRenderer* spriteRendererPtr)
{
	if (nullptr == spriteRendererPtr) return;
	HashedGuid spriteRendererGuid = spriteRendererPtr->GetInstanceID();
	SpinLock lock(m_proxyMapFlag);
	if (m_proxyMap.find(spriteRendererGuid) == m_proxyMap.end()) return;
	m_proxyMap[spriteRendererGuid]->DestroyProxy();
}

// ── 광원 ──
//
// 프리미티브와 같은 규약이되 저장소만 다르다(m_lightProxyMap · 자기 락).
// 광원은 프리미티브 맵을 순회하는 어느 경로에도 끼지 않아야 한다 —
// 그러라고 맵을 나눴다.

void RenderScene::RegisterCommand(LightComponent* lightPtr)
{
	if (nullptr == lightPtr) return;

	HashedGuid lightGuid = lightPtr->GetInstanceID();

	SpinLock lock(m_lightProxyMapFlag);

	if (m_lightProxyMap.find(lightGuid) != m_lightProxyMap.end()) return;

	m_lightProxyMap[lightGuid] = std::make_shared<LightRenderProxy>(lightPtr);
}

bool RenderScene::InvaildCheckLight(LightComponent* lightPtr)
{
	if (nullptr == lightPtr || lightPtr->IsDestroyMark()) return false;

	auto owner = lightPtr->GetOwner();
	if (nullptr == owner || owner->IsDestroyMark()) return false;

	HashedGuid lightGuid = lightPtr->GetInstanceID();

	SpinLock lock(m_lightProxyMapFlag);

	auto it = m_lightProxyMap.find(lightGuid);
	if (it == m_lightProxyMap.end()) return false;

	return nullptr != it->second;
}

void RenderScene::UpdateCommand(LightComponent* lightPtr)
{
	// 등록 전(첫 Awake 직전)이나 파괴 표시 뒤에는 조용히 넘긴다.
	// 다른 컴포넌트가 여기서 예외를 던지는 것은 갱신이 매 프레임이 아니라
	// 이벤트마다 오기 때문인데, 광원은 매 프레임 갱신이라 그 방식이면
	// 파괴되는 프레임마다 예외가 난다.
	if (!InvaildCheckLight(lightPtr)) return;

	ProxyCommand moveCommand = MakeProxyCommand(lightPtr);
	ProxyCommandQueue->PushProxyCommand(std::move(moveCommand));
}

ProxyCommand RenderScene::MakeProxyCommand(LightComponent* lightPtr)
{
	if (!InvaildCheckLight(lightPtr))
	{
		throw std::runtime_error("InvaildCheckLight");
	}

	ProxyCommand command(lightPtr);
	return command;
}

void RenderScene::UnregisterCommand(LightComponent* lightPtr)
{
	if (nullptr == lightPtr) return;

	HashedGuid lightGuid = lightPtr->GetInstanceID();

	SpinLock lock(m_lightProxyMapFlag);

	auto it = m_lightProxyMap.find(lightGuid);
	if (it == m_lightProxyMap.end() || nullptr == it->second) return;

	it->second->DestroyProxy();
}
