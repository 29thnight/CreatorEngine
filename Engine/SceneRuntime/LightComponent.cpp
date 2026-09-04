#include "LightComponent.h"
#include "RenderScene.h"
#include "LightSystem.h"

void LightComponent::OnInitialized()
{
    Scene* scene = GetOwner()->GetScene();
    if (scene == nullptr)
    {
        return;
    }

    // 씬의 광원 슬롯은 편집기 부기다 — 아이콘이 "메인 라이트"를 가리는 데
    // 쓰는 m_lightIndex와, Scene::DestroyLight의 유효 슬롯 압축이 전부다.
    // 그리는 데 쓰는 값은 아래 RegisterCommand가 만드는 프록시가 든다.
    if (-1 == m_lightIndex)
    {
        m_lightIndex = static_cast<int>(scene->AddLight());
    }
    else
    {
        scene->EnsureLightSlot(static_cast<size_t>(m_lightIndex));
    }
    scene->CollectLightComponent(this);

    if (auto* renderScene = SceneManagers->GetRenderScene())
    {
        renderScene->RegisterCommand(this);
    }
}

// 트랙 렌더: 씬 편입/이탈 시점에 LightSystem에 등록·해지한다(DDOL 안전 —
// 근거는 AnimatorSystem.h 상단 주석). 실 파괴 경로(FlushPendingDestroy)도
// OnUninitializing 직전에 OnRemovingFromScene을 먼저 부르므로, 이 시스템에서
// 빠지는 시점이 항상 실 파괴보다 먼저다.
void LightComponent::OnAddedToScene()
{
    LightSystems->Register(this);
	if (HasLifecycleState(State_Initialized) && GetOwner())
	{
		if (Scene* scene = GetOwner()->GetScene())
		{
			scene->CollectLightComponent(this);
			if (auto* renderScene = SceneManagers->GetRenderScene())
				renderScene->RegisterCommand(this);
		}
	}
}

void LightComponent::OnRemovingFromScene()
{
    LightSystems->Unregister(this);
	if (GetOwner() && !GetOwner()->IsDestroyMark())
	{
		if (Scene* scene = GetOwner()->GetScene())
		{
			scene->UnCollectLightComponent(this);
			if (auto* renderScene = SceneManagers->GetRenderScene())
				renderScene->UnregisterCommand(this);
		}
	}
}

void LightComponent::OnUninitializing()
{
    Scene* scene = GetOwner()->GetScene();
	if (scene != nullptr)
	{
		if (m_pOwner->IsDestroyMark()) scene->RemoveLight(m_lightIndex);
		scene->UnCollectLightComponent(this);
	}

    if (auto* renderScene = SceneManagers->GetRenderScene())
    {
        renderScene->UnregisterCommand(this);
    }
}
