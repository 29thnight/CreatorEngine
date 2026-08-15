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

namespace
{
void EnqueueProxyDelta(ProxyCommand command)
{
	if (command.IsValid())
	{
		ProxyCommandQueue->PushProxyCommand(std::move(command));
	}
}
}

void RenderScene::RegisterAnimator(const std::shared_ptr<Animator>& animatorPtr)
{
	m_animationJob.RegisterAnimator(animatorPtr);
}

void RenderScene::UnregisterAnimator(const std::shared_ptr<Animator>& animatorPtr)
{
	m_animationJob.UnregisterAnimator(animatorPtr);
}

void RenderScene::RegisterCommand(MeshRenderer* meshRendererPtr)
{
	if (nullptr == meshRendererPtr) return;

	EnqueueProxyDelta(ProxyCommand::CreatePrimitive(
		std::make_shared<MeshRenderProxy>(meshRendererPtr), GetSceneEpoch()));
}

void RenderScene::RegisterCommand(FoliageComponent* foliagePtr)
{
    if (nullptr == foliagePtr) return;

	EnqueueProxyDelta(ProxyCommand::CreatePrimitive(
		std::make_shared<FoliageRenderProxy>(foliagePtr), GetSceneEpoch()));
}

void RenderScene::UpdateCommand(MeshRenderer* meshRendererPtr)
{
	EnqueueProxyDelta(ProxyCommand(meshRendererPtr, GetSceneEpoch()));
}

void RenderScene::UpdateCommand(FoliageComponent* foliagePtr)
{
	EnqueueProxyDelta(ProxyCommand(foliagePtr, GetSceneEpoch()));
}

void RenderScene::UnregisterCommand(MeshRenderer* meshRendererPtr)
{
	if (nullptr == meshRendererPtr) return;

	EnqueueProxyDelta(ProxyCommand::DestroyPrimitive(
		meshRendererPtr->GetInstanceID(), GetSceneEpoch()));
}

void RenderScene::RegisterCommand(TerrainComponent* terrainPtr)
{
	if (nullptr == terrainPtr) return;
	EnqueueProxyDelta(ProxyCommand::CreatePrimitive(
		std::make_shared<TerrainRenderProxy>(terrainPtr), GetSceneEpoch()));
}

void RenderScene::UpdateCommand(TerrainComponent* terrainPtr)
{
	EnqueueProxyDelta(ProxyCommand(terrainPtr, GetSceneEpoch()));
}

void RenderScene::UnregisterCommand(TerrainComponent* terrainPtr)
{
	if (nullptr == terrainPtr) return;
	EnqueueProxyDelta(ProxyCommand::DestroyPrimitive(
		terrainPtr->GetInstanceID(), GetSceneEpoch()));
}

void RenderScene::UnregisterCommand(FoliageComponent* foliagePtr)
{
    if (nullptr == foliagePtr) return;

	EnqueueProxyDelta(ProxyCommand::DestroyPrimitive(
		foliagePtr->GetInstanceID(), GetSceneEpoch()));
}

void RenderScene::RegisterCommand(DecalComponent* decalPtr)
{
	if (nullptr == decalPtr) return;

	EnqueueProxyDelta(ProxyCommand::CreatePrimitive(
		std::make_shared<DecalRenderProxy>(decalPtr), GetSceneEpoch()));
}

void RenderScene::UpdateCommand(DecalComponent* decalPtr)
{
	EnqueueProxyDelta(ProxyCommand(decalPtr, GetSceneEpoch()));
}

void RenderScene::UnregisterCommand(DecalComponent* decalPtr)
{
	if (nullptr == decalPtr) return;

	EnqueueProxyDelta(ProxyCommand::DestroyPrimitive(
		decalPtr->GetInstanceID(), GetSceneEpoch()));
}

void RenderScene::RegisterCommand(ImageComponent* imagePtr)
{
	if (nullptr == imagePtr) return;
	EnqueueProxyDelta(ProxyCommand::CreateUI(
		std::make_shared<UIRenderProxy>(imagePtr), GetSceneEpoch()));
}

void RenderScene::UpdateCommand(ImageComponent* imagePtr)
{
	EnqueueProxyDelta(ProxyCommand(imagePtr, GetSceneEpoch()));
}

void RenderScene::UnregisterCommand(ImageComponent* imagePtr)
{
	if (nullptr == imagePtr) return;
	EnqueueProxyDelta(ProxyCommand::DestroyUI(
		imagePtr->GetInstanceID(), GetSceneEpoch()));
}

void RenderScene::RegisterCommand(TextComponent* textPtr)
{
	if (nullptr == textPtr) return;
	EnqueueProxyDelta(ProxyCommand::CreateUI(
		std::make_shared<UIRenderProxy>(textPtr), GetSceneEpoch()));
}

void RenderScene::UpdateCommand(TextComponent* textPtr)
{
	EnqueueProxyDelta(ProxyCommand(textPtr, GetSceneEpoch()));
}

void RenderScene::UnregisterCommand(TextComponent* textPtr)
{
	if (nullptr == textPtr) return;
	EnqueueProxyDelta(ProxyCommand::DestroyUI(
		textPtr->GetInstanceID(), GetSceneEpoch()));
}

void RenderScene::RegisterCommand(SpriteSheetComponent* spriteSheetPtr)
{
	if (nullptr == spriteSheetPtr) return;
	EnqueueProxyDelta(ProxyCommand::CreateUI(
		std::make_shared<UIRenderProxy>(spriteSheetPtr), GetSceneEpoch()));
}

void RenderScene::UnregisterCommand(SpriteSheetComponent* spriteSheetPtr)
{
	if (nullptr == spriteSheetPtr) return;
	EnqueueProxyDelta(ProxyCommand::DestroyUI(
		spriteSheetPtr->GetInstanceID(), GetSceneEpoch()));
}

void RenderScene::UpdateCommand(SpriteSheetComponent* spriteSheetPtr)
{
	EnqueueProxyDelta(ProxyCommand(spriteSheetPtr, GetSceneEpoch()));
}

void RenderScene::RegisterCommand(SpriteRenderer* spriteRendererPtr)
{
	if (nullptr == spriteRendererPtr) return;
	EnqueueProxyDelta(ProxyCommand::CreatePrimitive(
		std::make_shared<SpriteRenderProxy>(spriteRendererPtr), GetSceneEpoch()));
}

void RenderScene::UpdateCommand(SpriteRenderer* spriteRendererPtr)
{
	EnqueueProxyDelta(ProxyCommand(spriteRendererPtr, GetSceneEpoch()));
}

void RenderScene::UnregisterCommand(SpriteRenderer* spriteRendererPtr)
{
	if (nullptr == spriteRendererPtr) return;
	EnqueueProxyDelta(ProxyCommand::DestroyPrimitive(
		spriteRendererPtr->GetInstanceID(), GetSceneEpoch()));
}

// ── 광원 ──
//
// 프리미티브와 같은 규약이되 저장소만 다르다(m_lightProxyMap · 자기 락).
// 광원은 프리미티브 맵을 순회하는 어느 경로에도 끼지 않아야 한다 —
// 그러라고 맵을 나눴다.

void RenderScene::RegisterCommand(LightComponent* lightPtr)
{
	if (nullptr == lightPtr) return;

	EnqueueProxyDelta(ProxyCommand::CreateLight(
		std::make_shared<LightRenderProxy>(lightPtr), GetSceneEpoch()));
}

void RenderScene::UpdateCommand(LightComponent* lightPtr)
{
	EnqueueProxyDelta(ProxyCommand(lightPtr, GetSceneEpoch()));
}

void RenderScene::UnregisterCommand(LightComponent* lightPtr)
{
	if (nullptr == lightPtr) return;

	EnqueueProxyDelta(ProxyCommand::DestroyLight(
		lightPtr->GetInstanceID(), GetSceneEpoch()));
}
