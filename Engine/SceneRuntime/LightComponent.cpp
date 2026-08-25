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
        auto pair = scene->AddLight();
        m_lightIndex = static_cast<int>(pair.first);
        Light& light = pair.second;
        scene->CollectLightComponent(this);
        ApplyLightData(light);
    }
    else
    {
        auto& light = scene->GetLight(m_lightIndex);
        scene->CollectLightComponent(this);
        ApplyLightData(light);
    }

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
}

void LightComponent::OnRemovingFromScene()
{
    LightSystems->Unregister(this);
}

void LightComponent::OnUninitializing()
{
    Scene* scene = GetOwner()->GetScene();
    if (scene != nullptr && m_pOwner->IsDestroyMark())
    {
        scene->RemoveLight(m_lightIndex);
        scene->UnCollectLightComponent(this);
    }

    if (auto* renderScene = SceneManagers->GetRenderScene())
    {
        renderScene->UnregisterCommand(this);
    }
}

void LightComponent::ApplyLightData(Light& light)
{
    light.m_position = m_pOwner->Transform_().GetWorldPosition();
    light.m_direction = DirectX::XMVector3Rotate(DirectX::XMVectorSet(0, 0, 1, 0), m_pOwner->Transform_().GetWorldQuaternion());
    light.m_direction.Normalize();
    m_direction = light.m_direction;
    light.m_color = m_color * m_intencity;
    light.m_constantAttenuation = m_constantAttenuation;
    light.m_linearAttenuation = m_linearAttenuation;
    light.m_quadraticAttenuation = m_quadraticAttenuation;
    light.m_spotLightAngle = DirectX::XMConvertToRadians(m_spotLightAngle);
    light.m_lightType = static_cast<int>(m_lightType);
    light.m_lightStatus = static_cast<int>(m_lightStatus);
    light.m_range = m_range;
    light.m_intencity = m_intencity;
}
