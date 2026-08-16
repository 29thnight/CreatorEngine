#pragma once
#ifndef DYNAMICCPP_EXPORTS
#include "Component.h"
#include <cstdint>

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
    enum PhaseBits : uint16_t
    {
        Bit_None        = 0,
        Bit_Awake       = 1u << 0,
        Bit_OnEnable    = 1u << 1,
        Bit_Start       = 1u << 2,
        Bit_FixedUpdate = 1u << 3,
        Bit_Update      = 1u << 4,
        Bit_LateUpdate  = 1u << 5,
        Bit_OnDisable   = 1u << 6,
        Bit_OnDestroy   = 1u << 7,
    };

    /// T가 구현한 훅을 컴파일 타임에 판정한다.
    ///
    /// 비교 대상이 Component인 것이 핵심이다. 예전처럼 중간 인터페이스와 비교하면
    /// "그 인터페이스를 상속했는가"를 묻게 되지만, Component와 비교하면
    /// "기본 구현을 덮었는가"를 묻는다 — 후자가 실제로 알고 싶은 것이다.
    template<typename T>
    constexpr uint16_t MaskOfType()
    {
        uint16_t mask = Bit_None;
        if constexpr (&T::Awake       != &Component::Awake)       mask |= Bit_Awake;
        if constexpr (&T::OnEnable    != &Component::OnEnable)    mask |= Bit_OnEnable;
        if constexpr (&T::Start       != &Component::Start)       mask |= Bit_Start;
        if constexpr (&T::FixedUpdate != &Component::FixedUpdate) mask |= Bit_FixedUpdate;
        if constexpr (&T::Update      != &Component::Update)      mask |= Bit_Update;
        if constexpr (&T::LateUpdate  != &Component::LateUpdate)  mask |= Bit_LateUpdate;
        if constexpr (&T::OnDisable   != &Component::OnDisable)   mask |= Bit_OnDisable;
        if constexpr (&T::OnDestroy   != &Component::OnDestroy)   mask |= Bit_OnDestroy;
        return mask;
    }

    /// typeID → 마스크. 게임 스레드에서만 쓴다(등록은 기동 시 1회).
    class Registry
    {
    public:
        template<typename T>
        static void Register()
        {
            Store(TypeTrait::GUIDCreator::GetTypeID<T>(), MaskOfType<T>());

            // K1-a: 같은 자리에서 타입 인덱스(비트 위치)도 배정한다.
            // AddComponent(Meta::Type&) 같은 리플렉션 경로는 T를 모르고 typeID만
            // 들고 있어(GameObject.cpp) ComponentTypeIndex::Find(typeID)로 찾는다 —
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
#endif // !DYNAMICCPP_EXPORTS
