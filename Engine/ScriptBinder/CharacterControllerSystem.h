#pragma once
#include "ClassProperty.h"
#include <vector>

class CharacterControllerComponent;

// PHASE(SceneGraphRedesignPlan) 트랙 C3 — CharacterControllerComponent 가상
// FixedUpdate/LateUpdate 오버라이드의 시스템 이관. 정본은 AnimatorSystem.h/.cpp
// (같은 트랙, 먼저 이관)다 — 이 파일은 그 패턴을 그대로 따른다. 차이는
// CharacterControllerComponent가 Update가 아니라 FixedUpdate와 LateUpdate를
// 오버라이드했다는 것이라, 이 시스템의 진입점도 그 둘이다(Update는 없다).
//
// ── 무엇을 대신하는가 ──
//
// 예전에는 CharacterControllerComponent가 Component::FixedUpdate/LateUpdate를
// 오버라이드해 Lifecycle::Registry의 오버라이드 감지 마스크(Bit_FixedUpdate·
// Bit_LateUpdate)에 걸리고, Scene::RegisterComponent가 SystemSchedule::
// SubscribeImplicit으로 Scene 하나뿐인 m_schedule.FixedUpdateList()/
// LateUpdateList()에 다른 컴포넌트와 섞어 넣었다. 여기서는 전용 조밀
// std::vector<CharacterControllerComponent*>를 따로 두고 한 번에 순회한다.
//
// ── 등록/해지 훅 선택 근거 ──
//
// Awake/OnDestroy가 아니라 OnAddedToScene/OnRemovingFromScene(6단계 축, 게이트
// 없음)에 건다 — 근거는 AnimatorSystem.h와 동일(DDOL 오브젝트가 씬을 건널 때
// Awake는 컴포넌트당 1회만 불려 재등록되지 않는다). CharacterControllerComponent::
// Awake/OnDestroy는 Scene의 콜라이더 등록부(CollectColliderComponent/
// UnCollectColliderComponent, ICollider 축)용으로 그대로 남긴다(이 트랙 범위
// 밖 — 이 시스템의 등록/해지와 혼동 금지). OnStart(구 Start 오버라이드가 위임하던
// 몸통)도 손대지 않는다 — m_transform 캐시를 채우는 자리라 이 트랙과 무관하다.
//
// ── FixedUpdate 호출 횟수에 대한 주의 ──
//
// 이 저장소의 현재 구현(SceneManager::Physics → Scene::FixedUpdate, 프레임당
// 정확히 1회 호출 — Core::TimeSystem::Tick은 가변 timestep 분기만 쓰고 고정
// timestep 누적 루프를 쓰지 않는다, 2026-08-18 실측)에서는 FixedUpdate가
// 프레임당 정확히 1회 돈다. 그래도 이름이 암시하는 물리 스텝 카운트와 이 시스템의
// 틱 횟수가 어긋나면 안 되므로, 이 시스템은 프레임당 호출 횟수를 스스로 가정하지
// 않는다 — Scene이 FixedUpdate 페이즈를 부르는 횟수만큼 그대로 이 Update(tick)도
// 불려야 한다(배선 위치는 최종 보고서 참고).
//
// ── 실행 시점(호출 위치)은 이 시스템의 소관 밖 ──
//
// Scene.cpp는 다른 트랙이 동시 편집 중이라 이 파일에서 직접 배선하지 않는다.
// 옛 CharacterControllerComponent::FixedUpdate는 PhysicsManagers->Update(...)
// 보다 먼저 돌아 그 프레임의 캐릭터 이동 입력을 큐에 실었다(Physics->AddInputMove)
// — 이 시스템의 Update(tick)도 반드시 그 순서(PhysicsManagers->Update 이전)를
// 지켜야 한다. LateUpdate(tick)는 옛 LateUpdate 오버라이드와 같은 자리
// (Scene::LateUpdate의 RegistryTick(LateUpdateList) 인근)면 된다.
class CharacterControllerSystem : public Singleton<CharacterControllerSystem>
{
    friend class Singleton<CharacterControllerSystem>;
    CharacterControllerSystem() = default;
    ~CharacterControllerSystem() = default;

public:
    // 중복 등록은 조용히 무시한다(멱등) — 이유는 AnimatorSystem::Register와 같다.
    void Register(CharacterControllerComponent* controller);
    // 등록되어 있지 않으면 조용히 무시한다.
    void Unregister(CharacterControllerComponent* controller);

    // 등록된 컨트롤러 전부를 한 번에 틱한다(옛 FixedUpdate 대응). 옛
    // Scene::RegistryTick과 동일한 가드(owner 없음/파괴 표시/비활성 스킵)를
    // 이 시스템이 대신 적용한다.
    void FixedUpdate(float tick);

    // 등록된 컨트롤러 전부를 한 번에 틱한다(옛 LateUpdate 대응). 가드는 위와 같다.
    void LateUpdate(float tick);

    size_t GetCount() const noexcept { return m_controllers.size(); }

private:
    std::vector<CharacterControllerComponent*> m_controllers;
};

static auto CharacterControllerSystems = CharacterControllerSystem::GetInstance();
