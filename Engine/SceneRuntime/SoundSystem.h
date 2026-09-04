#pragma once
#include "ClassProperty.h"
#include <vector>

class SoundComponent;

// PHASE(SceneGraphRedesignPlan) 트랙 C3 — SoundComponent 가상 Update/LateUpdate
// 오버라이드의 시스템 이관. 정본은 AnimatorSystem.h/.cpp(같은 트랙, 먼저 이관)다 —
// 이 파일은 그 패턴을 그대로 따른다. 차이는 SoundComponent가 Update와
// LateUpdate 둘 다 오버라이드했다는 것뿐이라, 이 시스템도 진입점 둘을 갖는다.
//
// ── 무엇을 대신하는가 ──
//
// 예전에는 SoundComponent가 Component::Update/LateUpdate를 오버라이드해
// Lifecycle::Registry의 오버라이드 감지 마스크(옛 Bit_Update·Bit_LateUpdate —
// C3 완결로 철거)에 걸리고, Scene::RegisterComponent가 SystemSchedule::SubscribeImplicit으로
// Scene 하나뿐인 m_schedule.UpdateList()/LateUpdateList()에 스크립트 등
// 다른 컴포넌트와 섞어 넣었다. 여기서는 SoundComponent 전용 조밀
// std::vector<SoundComponent*>를 따로 두고 한 번에 순회한다.
//
// ── 등록/해지 훅 선택 근거 ──
//
// Awake/OnDestroy가 아니라 OnAddedToScene/OnRemovingFromScene(6단계 축, 게이트
// 없음)에 건다 — 근거는 AnimatorSystem.h와 동일(DDOL 오브젝트가 씬을 건널 때
// Awake는 컴포넌트당 1회만 불려 재등록되지 않는다). SoundComponent::OnDestroy는
// 기존 그대로(Stop() 호출, FMOD 채널 정지) 둔다 — RenderScene 류의 별도 등록부가
// 없어 Animator처럼 "다른 시스템 등록을 위해 남겨둘 Awake/OnDestroy"가 애초에
// 없다.
//
// ── 실행 시점(호출 위치)은 이 시스템의 소관 밖 ──
//
// Scene.cpp는 다른 트랙이 동시 편집 중이라 이 파일에서 직접 배선하지 않는다
// (배선 위치는 최종 보고서 참고 — Scene::Update는 RegistryTick(UpdateList) 직후,
// Scene::LateUpdate는 RegistryTick(LateUpdateList) 직후에 각각 대응하는 진입점을
// 불러야 한다. 옛 SoundComponent::Update/LateUpdate가 그 두 RegistryTick 안에서
// 돌던 것과 같은 상대 순서다).
class SoundSystem : public Singleton<SoundSystem>
{
    friend class Singleton<SoundSystem>;
    SoundSystem() = default;
    ~SoundSystem() = default;

public:
    // 중복 등록은 조용히 무시한다(멱등) — 이유는 AnimatorSystem::Register와 같다.
    void Register(SoundComponent* sound);
    // 등록되어 있지 않으면 조용히 무시한다.
    void Unregister(SoundComponent* sound);

    // 등록된 SoundComponent 전부를 한 번에 틱한다. 옛 SoundComponent::Update와
    // 동일한 가드(owner 없음/파괴 표시/비활성 스킵)를 이 시스템이 대신 적용한다.
    void Update(float tick);

    // 등록된 SoundComponent 전부를 한 번에 틱한다. 옛 SoundComponent::LateUpdate와
    // 동일한 가드를 이 시스템이 대신 적용한다.
    void LateUpdate(float tick);

    size_t GetCount() const noexcept { return m_sounds.size(); }

private:
    std::vector<SoundComponent*> m_sounds;
};

static auto SoundSystems = SoundSystem::GetInstance();
