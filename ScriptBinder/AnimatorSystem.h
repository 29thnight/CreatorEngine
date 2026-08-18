#pragma once
#include "ClassProperty.h"
#include <vector>

class Animator;

// PHASE(SceneGraphRedesignPlan) 트랙 C3 — Animator 가상 Update 오버라이드의 시스템 이관.
//
// ── 무엇을 대신하는가 ──
//
// 예전에는 Animator가 Component::Update를 오버라이드해 Lifecycle::Registry의
// 오버라이드 감지 마스크(Bit_Update)에 걸리고, Scene::RegisterComponent가
// SystemSchedule::SubscribeImplicit(component, Phase::Update)로 Scene 하나뿐인
// m_schedule.UpdateList()에 다른 컴포넌트(스크립트 포함)와 함께 섞어 넣었다.
// 여기서는 Animator 전용 조밀 std::vector<Animator*>를 따로 두고 한 번에
// 순회한다 — RenderScene::RegisterAnimator/AnimationJob의 스키닝용 등록부,
// AnimationJob의 잡 추적과는 목적이 다른 "로직 틱" 전용 등록부다(혼동 금지 —
// 그쪽은 건드리지 않는다).
//
// ── 등록/해지 훅 선택 근거 ──
//
// Awake/OnDestroy(Component.h 8훅 축, 컴포넌트당 1회 게이트)가 아니라
// OnAddedToScene/OnRemovingFromScene(6단계 축, 신설, 게이트 없음)에 건다.
// Animator::Awake/OnDestroy는 RenderScene 등록용으로 그대로 남기고(과제
// 지시대로 손대지 않는다), 이 시스템 등록은 별도 훅 쌍을 쓴다 — 이유는
// DDOL(DontDestroyOnLoad) 오브젝트가 씬을 건널 때다. Awake는
// Component::State_AwakeCalled 비트로 컴포넌트 평생 1회만 불리므로(
// Scene::RegistryDrainAwakeAndStart), DDOL 재부착 시 다시 불리지 않는다 —
// 만약 이 시스템 등록을 Awake에 걸면 "최초 생성 씬"의 등록부에서만 존재하고
// 새 씬으로 넘어간 뒤에는 영원히 틱을 못 받는(추적에서 영구 이탈하는) 결함이
// 새로 생긴다. 반대로 OnAddedToScene/OnRemovingFromScene은 게이트가 없어
// 씬에 들고 날 때마다(최초 생성 때도, DDOL Detach/Attach 때도) 매번 불린다
// (Scene::DetachGameObjectHierarchy·AttachExistingGameObject·
// AttachExistingGameObjectHierarchy가 각각 무조건 호출, Scene.cpp 확인).
// 실제 파괴 경로(FlushPendingDestroy)도 OnUninitializing(OnDestroy) 직전에
// OnRemovingFromScene을 먼저 부르므로, 이 시스템에서 빠지는 시점이 항상
// 실 파괴보다 먼저다 — 죽은 포인터를 틱할 창이 없다.
//
// ── 실행 시점(호출 위치)은 이 시스템의 소관 밖 ──
//
// Scene::Update가 일반 Update 페이즈(RegistryTick(m_schedule.UpdateList(), ...))
// 직후 이 Update(tick)를 불러야 실행 순서가 보존된다 — 17개 Animator 보유
// 프리팹 실측(Dynamic_CPP/Assets/Prefabs) 전부에서 스크립트(ModuleBehavior)가
// 루트 오브젝트에, Animator가 그 자식 오브젝트에 있고 루트가 먼저
// 인스턴스화·등록되므로, 옛 m_updateList 안에서도 스크립트 Update가 Animator
// Update보다 항상 먼저 돌았다(트리거 파라미터를 스크립트가 Update 안에서
// 세우고 Animator가 같은 프레임에 그 트리거를 소비 후 리셋하는 순서와 부합).
// Scene.cpp는 트랙 S2가 동시 편집 중이라 이 파일에서 직접 배선하지 않는다
// (배선 위치는 통합 보고서 참고).
class AnimatorSystem : public Singleton<AnimatorSystem>
{
    friend class Singleton<AnimatorSystem>;
    AnimatorSystem() = default;
    ~AnimatorSystem() = default;

public:
    // 중복 등록은 조용히 무시한다(멱등) — SystemSchedule의 암묵 경로와 달리
    // 이 등록부는 프레임마다 실제 로직(컨트롤러 Update+트리거 리셋)을 도는
    // 조밀 벡터라, 중복이 그대로 들어가면 같은 Animator가 한 프레임에 두 번
    // 틱해 트리거가 조기 소비되는 실질 오류가 된다(SystemSchedule처럼 "관측
    // 불변" 유지 목적의 중복 허용과는 사정이 다르다).
    void Register(Animator* animator);
    // 등록되어 있지 않으면 조용히 무시한다.
    void Unregister(Animator* animator);

    // 등록된 Animator 전부를 한 번에 틱한다. 옛 Animator::Update와 동일한 가드
    // (owner 없음/파괴 표시/비활성 스킵)를 이 시스템이 대신 적용한다 — 예전에는
    // Scene::RegistryTick이 공통으로 해주던 가드였다.
    void Update(float tick);

    size_t GetCount() const noexcept { return m_animators.size(); }

private:
    std::vector<Animator*> m_animators;
};

static auto AnimatorSystems = AnimatorSystem::GetInstance();
