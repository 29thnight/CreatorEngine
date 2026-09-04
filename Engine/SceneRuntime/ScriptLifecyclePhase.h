#pragma once
#include <cstdint>

// 관리 인스턴스 하나에 생명주기 단계 하나를 직접 전달할 때 쓰는 단계 번호
// (SceneGraphRedesignPlan 트랙 L · L3 완결).
//
// ── 이것이 왜 있나 ──
//
// 관리 측 생명주기의 드라이버를 **하나로** 만드는 전송로다(사용자 결정 2026-08-20).
// 예전에는 둘이었다 — 네이티브 ScriptComponent가 생성과 파괴만 알리고 그 사이
// 단계는 BehaviourRegistry가 자기 큐로 굴렸다. 그래서 네이티브에서만 일어나는
// 사건 — 대표적으로 DontDestroyOnLoad 이송 —이 관리 측에 닿지 않았다. 오브젝트는
// 살아서 씬을 건너는데 스크립트는 그 사실을 모르고, SimulationScope의 구독·대기가
// 옛 씬 기준으로 계속 흘렀다.
//
// ── 지금 상태 (L3 완결) ──
//
// **6단계 전부가 이 전송로를 지난다.** ScriptComponent가 여섯 훅을 모두
// 오버라이드해 NotifyManagedLifecycle로 넘기고, 관리 측 DispatchLifecycle이 받는다.
// 옛 관리 큐 _pendingAwake·_pendingStart는 **선언 자체가 없다** — BehaviourRegistry에
// 남은 _pendingAdd·_pendingRemove는 틱 멤버십(_active 편입/이탈)이지 생명주기
// 단계가 아니다.
//
// 관리 측 TearDown은 남아 있지만 폴백이다. 정상 경로에서는 축소의 첫 단계
// (OnEndSimulation)가 TeardownDelivered를 세우므로 TearDown이 곧바로 반환한다.
// 실제로 도는 경우는 **구동할 네이티브 컴포넌트가 없는** 둘뿐이다 — 고아 청소
// (SweepOrphans)와 어셈블리 리로드(Clear).
//
// 활성 축(OnEnable/OnDisable)은 6단계와 직교하므로 이 전송로로 오지 않는다.
// 예외적으로 OnEnable만 최초 OnAddedToScene 직후에 이어 붙는데(EnterDelivered
// 가드), 옛 드레인의 순서를 보존하기 위해서다.
//
// ── 값 규약 ──
//
// ScriptCore/BehaviourRegistry.cs의 LifecyclePhase와 **값이 같아야 한다.** 경계를
// 넘는 것은 int 하나이므로 컴파일러가 불일치를 잡아 주지 않는다. 한쪽을 고치면
// 반드시 다른 쪽도 고칠 것 — 순서는 Component.h의 6단계 선언 순서를 따른다.
//
// ★ 틱은 여기 오지 않는다. NetworkFrameworkPlan N3-b가 신설하는 고정 축 훅
//   OnSimulationTick(fixedDelta)은 **단계가 아니라 틱**이라 이 enum에 값을 더하지
//   않는다 — 기존 PrePhysics/PostPhysics와 같이 ClrHost의 전용 진입점으로 간다
//   (계획서 §4.4의 배선 6곳). 여기에 6번을 추가하려는 충동이 들면 그 표를 볼 것.
enum class ScriptLifecyclePhase : int32_t
{
    OnInitialized       = 0,
    OnAddedToScene      = 1,
    OnBeginSimulation   = 2,
    OnEndSimulation     = 3,
    OnRemovingFromScene = 4,
    OnUninitializing    = 5,
};
