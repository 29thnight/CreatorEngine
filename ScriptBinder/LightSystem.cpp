#include "LightSystem.h"
#include "LifecycleTrace.h"
#include "LightComponent.h"
#include "GameObject.h"
#include "SceneManager.h"
#include "RenderScene.h"
#include <algorithm>

void LightSystem::Register(LightComponent* light)
{
    if (nullptr == light) return;

    if (std::ranges::find(m_lights, light) != m_lights.end()) return;
    m_lights.push_back(light);
}

void LightSystem::Unregister(LightComponent* light)
{
    if (nullptr == light) return;

    // swap-and-pop — AnimatorSystem::Unregister와 같은 규약(단일 조밀 벡터라
    // 순서 보존은 애초에 불필요하고, erase는 O(n) 시프트라 씬 전환마다 비용이
    // 쌓인다).
    for (size_t i = 0; i < m_lights.size(); ++i)
    {
        if (m_lights[i] != light) continue;
        m_lights[i] = m_lights.back();
        m_lights.pop_back();
        return;
    }
}

void LightSystem::Update(float tick)
{
    // 옛 Scene::RegistryTick이 공통으로 해주던 가드(owner 없음/파괴 표시/
    // 비활성 스킵)를 이 시스템이 대신 적용한다 — LightComponent가 더 이상
    // m_schedule.UpdateList()를 거치지 않으므로 그 가드도 함께 옮겨왔다.
    for (LightComponent* light : m_lights)
    {
        if (nullptr == light) continue;

        Entity* owner = light->GetOwner();
        if (nullptr == owner || owner->IsDestroyMark()) continue;
        if (!light->IsEnabled()) continue;
        // 트랙 렌더 — 틱이 시스템으로 옮겨오면서 생명주기 트레이스의 발생지도
        // 함께 옮긴다. 안 남기면 이관할수록 기준선의 커버리지가 조용히 준다
        // (같은 문자열을 써야 대조가 성립하므로 Lifecycle::Trace::TypeNameOf
        // 공용 함수를 쓴다).
        LIFECYCLE_TRACE(Lifecycle::Phase::Update, Lifecycle::Trace::TypeNameOf(light),
            owner->m_name.ToString().c_str(), light->GetInstanceID());

        // ── 이하 옛 LightComponent::Update 본문 그대로(트랙 렌더 이관) ──
        // 예전 주석: 여기서 씬 광원 슬롯을 매 프레임 덮어썼다. 이제 갱신은
        // 프록시 커맨드 한 경로다 — 값이 흘러가는 자리를 하나로 두는 것이
        // 이 전환의 요점이고, 세기가 두 번 곱해지던 부류도 거기서 닫혔다.
        if (auto* renderScene = SceneManagers->GetRenderScene())
        {
            renderScene->UpdateCommand(light);
        }
    }
}
