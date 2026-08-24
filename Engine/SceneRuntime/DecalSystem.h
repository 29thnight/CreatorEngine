#pragma once
#include "ClassProperty.h"
#include <vector>

class DecalComponent;

// PHASE(SceneGraphRedesignPlan) 트랙 C3 — DecalComponent 가상 Update 오버라이드의
// 시스템 이관. 패턴은 AnimatorSystem과 동형이다 — 등록/해지 훅 선택 근거(왜
// Awake/OnDestroy가 아니라 OnAddedToScene/OnRemovingFromScene인가, DDOL이 왜
// 문제인가)와 파괴 경로 전수 확인(FlushPendingDestroy·DetachEntityHierarchy·
// PrefabUtility::ApplyComponentDiff가 전부 OnRemovingFromScene을 실 파괴보다
// 먼저 부른다는 근거)은 AnimatorSystem.h 상단 주석에 이미 있다 — 여기서는
// 반복하지 않는다.
//
// ── DecalComponent 고유 사정 ──
//
// 옛 DecalComponent::Update는 시스템의 공통 가드(owner 없음/파괴 표시/컴포넌트
// 비활성 — 옛 Scene::RegistryTick이 하던 것과 동일)와는 별개로, 자기 안에서
// GetOwner()->IsEnabled()를 한 번 더 본다. 이건 "이 컴포넌트가 켜져 있는가"
// (Component::IsEnabled(), 공통 가드가 이미 본다)가 아니라 "이 컴포넌트를
// 소유한 GameObject가 켜져 있는가"라 서로 다른 플래그다 — 둘이 겹치지 않으므로
// 공통 가드로 흡수하지 않고 옛 본문 그대로 시스템 Update 안에 남겨 뒀다.
// 지우면 오브젝트가 비활성인데도 슬라이스 애니메이션이 계속 도는 동작 변경이
// 된다.
//
// Awake/OnDestroy는 손대지 않는다 — DecalComponent가 거기서 하는 일
// (scene->CollectDecalComponent/UnCollectDecalComponent,
// renderScene->RegisterCommand/UnregisterCommand)은 이 트랙의 대상인
// "Update 오버라이드"와 무관한 별도 축(렌더 등록)이다.
class DecalSystem : public Singleton<DecalSystem>
{
    friend class Singleton<DecalSystem>;
    DecalSystem() = default;
    ~DecalSystem() = default;

public:
    // 중복 등록은 조용히 무시한다(멱등) — AnimatorSystem::Register와 같은 이유
    // (조밀 벡터가 매 프레임 실제 로직을 돌므로, 중복이 들어가면 같은
    // DecalComponent가 한 프레임에 두 번 틱해 슬라이스가 이중으로 진행된다).
    void Register(DecalComponent* decal);
    // 등록되어 있지 않으면 조용히 무시한다.
    void Unregister(DecalComponent* decal);

    // 등록된 DecalComponent 전부를 한 번에 틱한다. 옛 Scene::RegistryTick이
    // 공통으로 해주던 가드(owner 없음/파괴 표시/비활성 스킵)를 이 시스템이
    // 대신 적용한다.
    void Update(float tick);

    size_t GetCount() const noexcept { return m_decals.size(); }

private:
    std::vector<DecalComponent*> m_decals;
};

static auto DecalSystems = DecalSystem::GetInstance();
