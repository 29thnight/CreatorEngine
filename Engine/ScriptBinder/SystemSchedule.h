#pragma once
#include <vector>
#include <unordered_set>
#include <cstdint>

class Component;

// 페이즈 리스트 3종 + 구독 대상 관리를 한 타입으로 모은다
// (SceneGraphRedesignPlan §4 — 트랙 C의 C1과 트랙 L의 L4는 같은 구조물의 두
// 얼굴이다: C1은 "무엇을 들고 있는가"의 명명·구조화, L4는 "누가 들어오는가"의
// 명시 구독화).
//
// 예전에는 Scene이 std::vector<Component*> 여섯 개를 직접 멤버로 들고 있었다
// (m_pendingAwake·m_pendingStart·m_updateList·m_lateUpdateList·
// m_fixedUpdateList·m_destroyWatchList). 그 여섯이 여기로 옮겨 왔었다(C1).
//
// 그중 셋(m_updateList·m_lateUpdateList·m_fixedUpdateList)은 트랙 C3가
// Component의 가상 Update/LateUpdate/FixedUpdate를 통째로 걷어내고 네이티브
// 컴포넌트의 틱을 전용 시스템(AnimatorSystem·UITickSystem·SoundSystem 등)의
// 조밀 vector로 옮기면서 구독자가 0이 됐다 — 이 세 Phase로 Subscribe/
// SubscribeImplicit을 부르는 곳이 통째로 사라졌다. 남은 소비자는 진단
// (Scene::GetRegistryCounts·FireReentrancyStress의 로그) 둘뿐이었고, 그
// 진단이 세던 것도 결국 "아무도 안 쓰는 리스트의 크기"였다. 옛 RegistryTick도
// 그 시점에 소멸했다(3d8ff9a4) — 그래서 트랙 C3 완결에 맞춰 이 셋을 여기서
// 철거한다. 남은 셋(PendingAwake·PendingStart·DestroyWatch)은 지금도
// RegisterComponent/UnregisterComponent가 실제로 쓴다.
//
// 실행 순서(어느 페이즈를 언제 도는가)는 여전히 Scene::Awake/FixedUpdate/
// Update/LateUpdate/FlushPendingDestroy의 호출 순서가 정한다 — 이 타입은
// "무엇을 들고 있는가"만 관리하고 "언제 도는가"는 건드리지 않는다
// (회귀 세트 생명주기 순서 92 사건 불변이 이 슬라이스의 절대 게이트다).
class SystemSchedule
{
public:
    // Scene 내부의 세 리스트에 대응한다 — PendingAwake·PendingStart는 지연
    // 편입 큐, DestroyWatch는 파괴 감시 목록이다. Update/LateUpdate/
    // FixedUpdate는 트랙 C3 완결로 철거했다(위 클래스 주석 참고) — 네이티브
    // 컴포넌트의 프레임 틱은 이제 이 타입을 거치지 않고 전용 시스템의 조밀
    // vector가 돈다. 선언 순서는 프레임 진행 순서를 나타내지 않는다 — 실제
    // 진행 순서는 Scene.cpp의 함수 호출 순서(FixedUpdate n회 → Update(Pre
    // 행렬·틱·행렬) → LateUpdate(틱·렌더 커밋), 파괴 통지는 프레임 끝
    // FlushPendingDestroy 한 지점)를 그대로 봐야 한다.
    //
    // Lifecycle::PhaseBits(LifecycleRegistry.h)와는 다른 열거형이다 — PhaseBits는
    // "어느 훅을 오버라이드했는가"를 나타내는 마스크 비트(OnEnable·OnDisable처럼
    // 전용 리스트가 없는 축도 포함)이고, 이 Phase는 "이 스케줄의 어느 저장소인가"만
    // 가리킨다. 둘을 하나로 합치면 리스트가 없는 축까지 저장소를 요구하게 되어
    // 오히려 틀린 모델이 된다.
    enum class Phase : uint8_t
    {
        PendingAwake,
        PendingStart,
        DestroyWatch,
    };

    // ── 저장소 접근 ──
    //
    // 기존 RegistryDrainAwakeAndStart/FlushPendingDestroy가 swap·인덱스 순회로
    // 벡터를 직접 다루던 자리는 그대로 Scene.cpp에 남는다 — 여기서는 그 벡터가
    // 사는 자리만 옮겼다. 재진입 안전을 위한 swap
    // (예: `awaking.swap(m_schedule.PendingAwakeList())`)도 그대로 성립한다.
    // UpdateList/LateUpdateList/FixedUpdateList는 트랙 C3 완결로 철거했다 —
    // 옛 RegistryTick이 이 자리에서 그 셋을 순회하며 컴포넌트의 가상 Update/
    // LateUpdate/FixedUpdate를 불렀는데, RegistryTick 자체가 소멸하며(위 클래스
    // 주석 참고) 이 접근자들도 부를 곳이 없어졌다.
    std::vector<Component*>& PendingAwakeList() { return m_pendingAwake; }
    std::vector<Component*>& PendingStartList() { return m_pendingStart; }
    std::vector<Component*>& DestroyWatchList() { return m_destroyWatchList; }

    const std::vector<Component*>& PendingAwakeList() const { return m_pendingAwake; }
    const std::vector<Component*>& PendingStartList() const { return m_pendingStart; }
    const std::vector<Component*>& DestroyWatchList() const { return m_destroyWatchList; }

    // ── 명시 구독 API (트랙 L4 정본) ──
    //
    // 새 컴포넌트는 훅 오버라이드 없이 이 호출만으로 틱을 받을 수 있다 — 보통
    // OnBeginSimulation에서 자신을 구독하고(SimulationScope 귀속은 후속 트랙
    // 소관), OnEndSimulation/OnRemovingFromScene에서 Unsubscribe/UnsubscribeAll로
    // 뗀다. 같은 페이즈에 두 번 구독하면 리스트에 두 번 들어간다 — 기존 암묵
    // 경로(RegisterComponent)도 중복 검사 없이 push_back하는 같은 규약이라
    // 여기서 새로 막지 않는다(관측 불변).
    void Subscribe(Component* component, Phase phase);
    // 한 페이즈에서만 뗀다. 컴포넌트가 다른 페이즈에도 구독 중이면 그쪽은 남는다
    // — 전부 뗄 때는 UnsubscribeAll을 쓴다.
    void Unsubscribe(Component* component, Phase phase);
    // 전 페이즈에서 뗀다. 기존 UnregisterComponent가 여섯 줄로 하던 RemoveFromList
    // 반복을 대신한다.
    void UnsubscribeAll(Component* component);

    // ── 암묵 구독 (RegisterComponent 전용 내부 경로) ──
    //
    // Lifecycle::Registry의 오버라이드 감지 마스크가 판정한 편입은 이 함수를
    // 지난다. 저장소에 들어가는 동작은 Subscribe와 완전히 같다 — 다른 것은
    // 아래 GetSubscriptionCounts()가 세는 장부뿐이다.
    void SubscribeImplicit(Component* component, Phase phase);

    // ── 암묵/명시 구독 잔존 수 (트랙 L4 래칫 측정 기반) ──
    //
    // "잔존"이므로 컴포넌트가 UnsubscribeAll로 완전히 빠지면 줄어든다. 부분 해지
    // (Unsubscribe, 페이즈 하나만)는 건드리지 않는다 — 다른 페이즈에 남아 있을
    // 수 있어서다. 프로파일러 연동은 범위 밖 — 카운터만 노출한다.
    struct SubscriptionCounts
    {
        size_t implicitCount;
        size_t explicitCount;
    };
    SubscriptionCounts GetSubscriptionCounts() const;

    // Scene 소멸자·Reset 경로용 일괄 비움.
    void Clear();

private:
    std::vector<Component*>& ListFor(Phase phase);

    std::vector<Component*> m_pendingAwake;
    std::vector<Component*> m_pendingStart;
    std::vector<Component*> m_destroyWatchList;

    // 컴포넌트별 편입 경로. UnsubscribeAll에서 어느 카운터를 줄일지 여기서
    // 판정한다 — 같은 컴포넌트가 여러 페이즈에 걸쳐 있어도 집합이라 중복
    // 계산되지 않는다(암묵 경로가 한 컴포넌트를 여러 리스트에 동시에 밀어
    // 넣는 것이 정상 동작이므로).
    std::unordered_set<Component*> m_implicitOrigin;
    std::unordered_set<Component*> m_explicitOrigin;
};

// ── 구현부 (헤더 전용) ──
//
// 통합 단계 메모: 원래 SystemSchedule.cpp로 분리되어 있었으나, ScriptBinder.vcxproj가
// 이번 통합 세션에서 수정 금지 대상이라(신규 ClCompile 등록 불가) 링크 실패를 피하기
// 위해 헤더 전용(inline)으로 합쳤다. EntityHandle.h 선례와 동일한 패턴 — vcxproj 등록이
// 회귀 세트를 통과하고 나면(L4 보고서 후속 배선 항목) 별도 .cpp로 다시 분리해도 무방하다.
namespace SystemSchedule_Internal
{
    // Scene.cpp의 옛 RemoveFromList와 동일한 규약: swap-and-pop, 순서 비보존.
    // 보존해야 하는 것은 단계 사이의 순서이지 같은 단계 안의 순서가 아니고,
    // erase로 앞당기면 제거가 O(n)이 되어 씬 전환마다 수천 번 돈다(옛 주석 그대로 승계).
    inline bool RemoveFromList(std::vector<Component*>& list, Component* target)
    {
        for (size_t i = 0; i < list.size(); ++i)
        {
            if (list[i] != target) continue;
            list[i] = list.back();
            list.pop_back();
            return true;
        }
        return false;
    }
}

inline std::vector<Component*>& SystemSchedule::ListFor(Phase phase)
{
    switch (phase)
    {
    case Phase::PendingAwake:  return m_pendingAwake;
    case Phase::PendingStart:  return m_pendingStart;
    case Phase::DestroyWatch:  return m_destroyWatchList;
    }
    // 전 열거값을 위에서 다뤘다 — 여기 닿으면 새 Phase가 추가되고 이 switch가
    // 안 갱신된 것이다(Update/LateUpdate/FixedUpdate는 트랙 C3 완결로 철거했다
    // — 위 클래스 주석 참고). 크래시 대신 방어적으로라도 값을 돌려줘야 컴파일이
    // 성립하므로 첫 리스트로 고정한다.
    return m_pendingAwake;
}

inline void SystemSchedule::Subscribe(Component* component, Phase phase)
{
    if (nullptr == component) return;
    ListFor(phase).push_back(component);
    m_explicitOrigin.insert(component);
}

inline void SystemSchedule::SubscribeImplicit(Component* component, Phase phase)
{
    if (nullptr == component) return;
    ListFor(phase).push_back(component);
    m_implicitOrigin.insert(component);
}

inline void SystemSchedule::Unsubscribe(Component* component, Phase phase)
{
    if (nullptr == component) return;
    SystemSchedule_Internal::RemoveFromList(ListFor(phase), component);
}

inline void SystemSchedule::UnsubscribeAll(Component* component)
{
    if (nullptr == component) return;

    SystemSchedule_Internal::RemoveFromList(m_pendingAwake, component);
    SystemSchedule_Internal::RemoveFromList(m_pendingStart, component);
    SystemSchedule_Internal::RemoveFromList(m_destroyWatchList, component);

    m_implicitOrigin.erase(component);
    m_explicitOrigin.erase(component);
}

inline SystemSchedule::SubscriptionCounts SystemSchedule::GetSubscriptionCounts() const
{
    return SubscriptionCounts{ m_implicitOrigin.size(), m_explicitOrigin.size() };
}

inline void SystemSchedule::Clear()
{
    m_pendingAwake.clear();
    m_pendingStart.clear();
    m_destroyWatchList.clear();

    m_implicitOrigin.clear();
    m_explicitOrigin.clear();
}
