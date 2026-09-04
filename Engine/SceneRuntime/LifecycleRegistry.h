#pragma once
#include "Component.h"
#include <cstdint>
#include <memory>
#include <type_traits>

// 컴포넌트 타입별 생명주기 구현 여부 표 (PHASE 9-1).
//
// ── 무엇을 대신하는가 ──
//
// 예전에는 RegistableEvent<T>가 `if constexpr (&T::Update != &IRegistableEvent::Update)`로
// 타입마다 판정하고, 그 자리에서 델리게이트에 람다를 등록했다. 판정 자체는 공짜였지만
// 세 가지가 딸려 왔다: (1) 등록이 곧 구독이라 판정과 디스패치가 붙어 버렸고,
// (2) RegisterOverriddenEvents가 final이라 파생 클래스가 재판정할 수 없어 중간 클래스가
// 구현하지 않은 훅을 손자가 구현하면 조용히 누락됐으며, (3) 결과가 어디에도 남지 않아
// 매번 다시 계산됐다.
//
// 여기서는 판정을 **타입당 한 번** 하고 결과를 비트마스크로 남긴다. 디스패치는 그
// 마스크를 읽는 쪽(Scene)이 알아서 한다 — 판정과 디스패치가 분리된다.
//
// ── 왜 Meta::Type이 아니라 별도 표인가 ──
//
// 계획에는 Meta::Type에 비트를 넣는다고 적었는데, Meta::Type은 MetaGenerator가 만드는
// 산출물이라 필드를 늘리려면 생성기를 고쳐야 한다. 이 슬라이스의 범위를 넘고, 생성기
// 변경은 전 타입의 .generated.h를 재생성시켜 diff가 이 작업과 섞인다.
// typeID를 키로 하는 옆 표는 같은 효과를 내면서 생성기를 건드리지 않는다.
namespace Lifecycle
{
    // 트랙 L1: 8비트(Awake~OnDestroy) → 11비트(6단계 + OnEnable/OnDisable + 틱3).
    // 트랙 C3 완결 후 8비트 — 틱 3비트를 뺐다(아래).
    //
    // Bit_OnInitialized·Bit_OnBeginSimulation·Bit_OnUninitializing은 옛 이름
    // (Bit_Awake·Bit_Start·Bit_OnDestroy)의 후신이다 — 비트 값(자리)은 그대로다.
    // 나머지 셋(Bit_OnAddedToScene·Bit_OnEndSimulation·Bit_OnRemovingFromScene)은
    // 대응하는 옛 훅이 없는 신설 축이라 새 자리를 받는다.
    //
    // ★ 옛 Bit_FixedUpdate(1u<<3)·Bit_Update(1u<<4)·Bit_LateUpdate(1u<<5)는
    //   철거했다. 트랙 C3가 Component에서 가상 Update/LateUpdate/FixedUpdate를
    //   통째로 걷어낸 순간 아래 MaskOfType의 `&T::Update != &Component::Update`
    //   비교가 성립할 수 없게 됐고, 그때부터 이 셋은 **세우는 코드가 하나도 없는
    //   영구 0**이었다. 값만 남은 비트는 "언젠가 서는 것"으로 읽혀 오히려
    //   판단을 방해한다 — 전용 시스템의 조밀 배열이 무엇을 언제 도는지 전부
    //   말하므로(SystemSchedule.h 클래스 주석) 이 축은 되살아나지 않는다.
    //
    //   자리(1u<<3~5)는 비워 둔다. NetworkFrameworkPlan N3-b가 신설하는 고정 축
    //   훅 OnSimulationTick은 **관리 측 전용**이라(배선 6곳에 Component.h가 없다)
    //   네이티브 비트를 요구하지 않는다. 네이티브 컴포넌트에도 고정 축 훅이
    //   필요해지면 그때 축을 드러내는 새 이름으로 이 자리를 받는다.
    enum PhaseBits : uint16_t
    {
        Bit_None                 = 0,
        Bit_OnInitialized        = 1u << 0,
        Bit_OnEnable             = 1u << 1,
        Bit_OnBeginSimulation    = 1u << 2,
        Bit_OnDisable            = 1u << 6,
        Bit_OnUninitializing     = 1u << 7,
        Bit_OnAddedToScene       = 1u << 8,
        Bit_OnEndSimulation      = 1u << 9,
        Bit_OnRemovingFromScene  = 1u << 10,
    };

    /// T가 구현한 훅을 컴파일 타임에 판정한다.
    ///
    /// 비교 대상이 Component인 것이 핵심이다. 예전처럼 중간 인터페이스와 비교하면
    /// "그 인터페이스를 상속했는가"를 묻게 되지만, Component와 비교하면
    /// "기본 구현을 덮었는가"를 묻는다 — 후자가 실제로 알고 싶은 것이다.
    ///
    /// ★ 전환기 브리지는 없다(트랙 L · L3 완결). 옛 Awake/Start/OnDestroy와 그
    /// 기본 구현이 새 훅을 부르던 다리를 걷어냈으므로, 지금 이 판정이 묻는 것은
    /// 오직 "6단계 훅 중 무엇을 덮었는가" 하나다. 옛 이름은 Component에 존재하지
    /// 않아 비교 대상 자체가 없다.
    ///
    /// 이 함수는 NetworkFrameworkPlan N3-a(관리 측 오버라이드 감지)가 선례로
    /// 지목한 자리다 — 관리 측은 컴파일 타임이 아니므로 타입당 1회 계산해
    /// 캐시하지만, "덮은 것만 디스패치 목록에 넣는다"는 규약은 같다.
    template<typename T>
    constexpr uint16_t MaskOfType()
    {
        uint16_t mask = Bit_None;
        if constexpr (&T::OnInitialized       != &Component::OnInitialized)      mask |= Bit_OnInitialized;
        if constexpr (&T::OnEnable           != &Component::OnEnable)            mask |= Bit_OnEnable;
        if constexpr (&T::OnBeginSimulation   != &Component::OnBeginSimulation)  mask |= Bit_OnBeginSimulation;
        if constexpr (&T::OnDisable          != &Component::OnDisable)           mask |= Bit_OnDisable;
        if constexpr (&T::OnUninitializing    != &Component::OnUninitializing)   mask |= Bit_OnUninitializing;
        if constexpr (&T::OnAddedToScene     != &Component::OnAddedToScene)      mask |= Bit_OnAddedToScene;
        if constexpr (&T::OnEndSimulation    != &Component::OnEndSimulation)     mask |= Bit_OnEndSimulation;
        if constexpr (&T::OnRemovingFromScene != &Component::OnRemovingFromScene) mask |= Bit_OnRemovingFromScene;
        return mask;
    }

    /// typeID → 마스크. 게임 스레드에서만 쓴다(등록은 기동 시 1회).
    class Registry
    {
    public:
        template<typename T>
        static void Register()
        {
            // K2 불변식: 컴포넌트가 enable_shared_from_this를 상속하면 여기서
            // 컴파일이 막힌다 — Animator가 shared_from_this()로 AnimationJob에
            // 소유권을 흘려 unique_ptr 전환을 막았던 사건의 재발 방지.
            static_assert(!std::is_base_of_v<std::enable_shared_from_this<T>, T>,
                "컴포넌트는 enable_shared_from_this를 상속하지 마라 — 소유권은 GameObject가 갖고, "
                "다른 시스템은 raw 포인터로 관찰만 해야 한다(Animator/AnimationJob 사건 참조).");

            Store(TypeTrait::GUIDCreator::GetTypeID<T>(), MaskOfType<T>());

            // K1-a: 같은 자리에서 타입 인덱스(비트 위치)도 배정한다.
            // AddComponent(Meta::Type&) 같은 리플렉션 경로는 T를 모르고 typeID만
            // 들고 있어(Entity.cpp) ComponentTypeIndex::Find(typeID)로 찾는다 —
            // 여기서 먼저 Get<T>()를 불러 두지 않으면 그 경로는 항상 kInvalid를 받는다.
            TypeTrait::ComponentTypeIndex::Get<T>();
        }

        /// 등록되지 않은 타입이면 kUnregistered를 돌려준다.
        /// 0(=아무 훅도 없음)과 구분되어야 한다 — 둘을 섞으면 등록을 빠뜨린 타입이
        /// "훅이 없는 타입"으로 조용히 처리되고, 그것이 예전 CRTP의 실패 방식이다.
        static uint16_t Find(size_t typeID);

        static constexpr uint16_t kUnregistered = 0xFFFFu;

        static size_t Count();

        /// 엔진이 아는 모든 컴포넌트 타입을 등록한다(기동 시 1회).
        /// 구현은 LifecycleRegistry.cpp의 명시 목록이다.
        static void RegisterAllComponents();

    private:
        static void Store(size_t typeID, uint16_t mask);
    };
}
