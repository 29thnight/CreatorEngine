#pragma once
#include <cstdint>

// 엔티티(GameObject)가 씬 그래프 위에서 놓인 단계 (SceneGraphRedesignPlan §4 트랙 L1).
//
// Existence(씬에 존재)와 Simulation(시뮬레이션 참가)이 별개 축이라는 것이 이
// enum의 요점이다 — 에디터에서 씬을 열어 오브젝트가 InScene이라고 해서
// Simulating인 것은 아니다. 네 단계는 선형이다:
//
//   Detached → Attached → InScene → Simulating
//
// 각 화살표가 Component의 생명주기 훅 하나에 대응한다(OnInitialized ·
// OnAddedToScene · OnBeginSimulation, 역방향은 OnEndSimulation ·
// OnRemovingFromScene · OnUninitializing). 지금 엔티티는 생성과 동시에 씬에
// 편입되므로(Scene::CreateGameObject 등이 슬롯 할당과 등록을 한 호출 안에서
// 끝낸다) Detached·Attached는 사실상 DDOL 이송 창에서만 관측된다.
enum class ScenePhase : uint8_t
{
    Detached,
    Attached,
    InScene,
    Simulating,
};
