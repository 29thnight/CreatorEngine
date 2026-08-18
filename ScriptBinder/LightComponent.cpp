#include "LightComponent.h"
#include "RenderScene.h"

void LightComponent::Awake()
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

void LightComponent::Update(float deltaSeconds)
{
    // 예전에는 여기서 씬 광원 슬롯을 매 프레임 덮어썼다. 이제 갱신은
    // 프록시 커맨드 한 경로다 — 값이 흘러가는 자리를 하나로 두는 것이
    // 이 전환의 요점이고, 세기가 두 번 곱해지던 부류도 거기서 닫혔다.
    if (auto* renderScene = SceneManagers->GetRenderScene())
    {
        renderScene->UpdateCommand(this);
    }
}

void LightComponent::OnDestroy()
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
    light.m_direction = XMVector3Rotate(XMVectorSet(0, 0, 1, 0), m_pOwner->Transform_().GetWorldQuaternion());
    light.m_direction.Normalize();
    m_direction = light.m_direction;
    light.m_color = m_color * m_intencity;
    light.m_constantAttenuation = m_constantAttenuation;
    light.m_linearAttenuation = m_linearAttenuation;
    light.m_quadraticAttenuation = m_quadraticAttenuation;
    light.m_spotLightAngle = XMConvertToRadians(m_spotLightAngle);
    light.m_lightType = static_cast<int>(m_lightType);
    light.m_lightStatus = static_cast<int>(m_lightStatus);
    light.m_range = m_range;
    light.m_intencity = m_intencity;
}
