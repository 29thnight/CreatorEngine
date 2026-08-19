#include "DecalSystem.h"
#include "LifecycleTrace.h"
#include "DecalComponent.h"
#include "GameObject.h"
#include <algorithm>

void DecalSystem::Register(DecalComponent* decal)
{
    if (nullptr == decal) return;

    if (std::ranges::find(m_decals, decal) != m_decals.end()) return;
    m_decals.push_back(decal);
}

void DecalSystem::Unregister(DecalComponent* decal)
{
    if (nullptr == decal) return;

    // swap-and-pop — AnimatorSystem::Unregister와 같은 규약(단일 조밀 벡터라
    // 순서 보존은 애초에 불필요하고, erase의 O(n) 시프트는 씬 전환마다 비용이
    // 쌓인다).
    for (size_t i = 0; i < m_decals.size(); ++i)
    {
        if (m_decals[i] != decal) continue;
        m_decals[i] = m_decals.back();
        m_decals.pop_back();
        return;
    }
}

void DecalSystem::Update(float tick)
{
    // 옛 Scene::RegistryTick이 공통으로 해주던 가드(owner 없음/파괴 표시/
    // 비활성 스킵)를 이 시스템이 대신 적용한다 — DecalComponent가 더 이상
    // m_schedule.UpdateList()를 거치지 않으므로 그 가드도 함께 옮겨왔다.
    for (DecalComponent* decal : m_decals)
    {
        if (nullptr == decal) continue;

        Entity* owner = decal->GetOwner();
        if (nullptr == owner || owner->IsDestroyMark()) continue;
        if (!decal->IsEnabled()) continue;
        // C3 — 틱이 시스템으로 옮겨오면서 생명주기 트레이스의 발생지도 함께 옮긴다.
        // 안 남기면 이관할수록 기준선의 커버리지가 조용히 준다(같은 문자열을 써야
        // 대조가 성립하므로 Lifecycle::Trace::TypeNameOf 공용 함수를 쓴다).
        LIFECYCLE_TRACE(Lifecycle::Phase::Update, Lifecycle::Trace::TypeNameOf(decal),
            owner->m_name.ToString().c_str(), decal->GetInstanceID());

        // ── 이하 옛 DecalComponent::Update 본문 그대로(트랙 C3 이관) ──
        // owner->IsEnabled()는 위 공통 가드의 decal->IsEnabled()와는 다른
        // 플래그다(컴포넌트 자신 vs 소유 GameObject) — 의도적으로 남겨 둔다
        // (DecalSystem.h 상단 주석 참고).
        if (!owner->IsEnabled() || !decal->useAnimation) continue;

        // slicePerSeconds는 저작값이고 0(기본값)이나 음수가 그대로 들어온다 —
        // 그러면 아래 while이 절대 끝나지 않아 프레임이 그 자리에서 잠긴다.
        // C3 이관 때 원본을 바이트 그대로 옮기며 함께 넘어온 선행 결함이다.
        if (decal->slicePerSeconds <= 0.f) continue;

        decal->timer += tick;
        while (decal->timer >= decal->slicePerSeconds)
        {
            decal->timer -= decal->slicePerSeconds;
            decal->sliceNumber++;
        }
        if (decal->isLoop)
            decal->sliceNumber = decal->sliceNumber % (decal->sliceX * decal->sliceY);
        else
            decal->sliceNumber = decal->sliceX * decal->sliceY - 1;
    }
}
