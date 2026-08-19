#pragma once
#include <cstdint>

// 관리 인스턴스 하나에 생명주기 단계 하나를 직접 전달할 때 쓰는 단계 번호
// (SceneGraphRedesignPlan 트랙 L · L3 잔여).
//
// ── 이것이 왜 있나 ──
//
// 지금 관리 측 생명주기의 드라이버는 둘이다. 네이티브 ScriptComponent는 인스턴스의
// 생성(OnInitialized)과 파괴(OnUninitializing)만 알리고, 그 사이 네 단계는
// BehaviourRegistry가 **자기 큐로** 굴린다(_pendingAwake/_pendingStart/_pendingRemove).
// 그래서 네이티브에서만 일어나는 사건 — 대표적으로 DontDestroyOnLoad 이송 —이
// 관리 측에 전혀 닿지 않는다. 오브젝트는 살아서 씬을 건너는데 스크립트는 그 사실을
// 모르고, SimulationScope의 구독·대기가 옛 씬 기준으로 계속 흐른다.
//
// ── 방향은 "드라이버를 하나로"다 (사용자 결정 2026-08-20) ──
//
// 최종형은 BehaviourRegistry가 자체 큐를 걷고 6단계 전부를 네이티브에서 받는 것이다.
// 드라이버가 하나면 중복 발화가 **표현 불가능**해진다. 이 파일이 그 전송로다 —
// 지금은 씬 편입/이탈 두 단계만, 그것도 이송 경로에서만 태우고, 나머지 단계가
// 차례로 이 위로 올라온다.
//
// ── 왜 지금 전부 태우지 않나 ──
//
// 네이티브 훅을 그대로 전달하면 두 곳에서 이중 발화한다:
//   ⓐ Scene의 드레인이 OnInitialized 직후 OnAddedToScene을 부르는데, 그 시점
//      관리 인스턴스는 아직 _pendingAwake에 있어 나중에 같은 단계를 또 받는다.
//   ⓑ Scene::FlushPendingDestroy가 **모든 파괴에서** OnEndSimulation과
//      OnRemovingFromScene을 부르고, 그 뒤 DestroyBehaviour가 부르는 관리 측
//      TearDown이 같은 둘을 또 부른다.
// 둘 다 관리 측 큐를 걷어야 사라진다 — 그것이 남은 작업이고, 이 전송로는 그 작업이
// 진행되는 동안 형태가 바뀌지 않는다.
//
// ── 값 규약 ──
//
// ScriptCore/BehaviourRegistry.cs의 LifecyclePhase와 **값이 같아야 한다.** 경계를
// 넘는 것은 int 하나이므로 컴파일러가 불일치를 잡아 주지 않는다. 한쪽을 고치면
// 반드시 다른 쪽도 고칠 것 — 순서는 Component.h의 6단계 선언 순서를 따른다.
enum class ScriptLifecyclePhase : int32_t
{
    OnInitialized       = 0,
    OnAddedToScene      = 1,
    OnBeginSimulation   = 2,
    OnEndSimulation     = 3,
    OnRemovingFromScene = 4,
    OnUninitializing    = 5,
};
