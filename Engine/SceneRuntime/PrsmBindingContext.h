#pragma once
// PrSM 스크립트 호출 하나의 신뢰 문맥 (엔진 바인딩 검증용).
//
// 생성된 바인딩 래퍼는 엔진 객체에 닿기 전에 "지금 게임 스레드에서, 엔진이 연
// PrSM 호출 안에 있는가"를 확인해야 한다(reflgen PrSM production ABI v1). 그 사실을
// 아는 것은 호출을 연 PrsmScriptHost 뿐이므로, 호스트가 호출마다 이 범위를 세운다.
//
// 표준 헤더만 쓴다 — reflgen 이 생성한 브리지 번역 단위가 어댑터를 거쳐 이것을 본다.
#include <cstdint>

class Entity;

namespace prsm_engine
{
    // 스크립트에 넘기는 엔티티 핸들의 비트 배치. ScriptObjectRegistry 의 슬롯·세대를
    // 그대로 쓴다: 하위 32비트 슬롯, 상위 32비트 세대(0 은 무효). 주소가 아니다.
    // receiver("entity")·receiver("component") 타입은 모두 이 핸들을 받는다 —
    // 컴포넌트 전용 핸들은 없다(C# 경로와 같은 엔티티 세대 핸들).
    inline constexpr std::uint64_t EncodeEntityHandle(std::uint32_t index, std::uint32_t generation) noexcept
    {
        return (static_cast<std::uint64_t>(generation) << 32) | index;
    }

    struct CallContext
    {
        Entity* owner = nullptr;          // 실행 중인 PrSM 컴포넌트가 붙은 엔티티(코루틴 폴링 중에는 null)
        std::uint64_t ownerHandle = 0;    // owner 의 인코딩된 핸들(인스턴스 생성 때 한 번 등록)
    };

    // 중첩 가능하다(엔진 → 스크립트 → 엔진 → 스크립트). 소멸이 이전 문맥을 되돌린다.
    class CallScope
    {
    public:
        CallScope(Entity* owner, std::uint64_t ownerHandle) noexcept;
        ~CallScope();
        CallScope(const CallScope&) = delete;
        CallScope& operator=(const CallScope&) = delete;

        static const CallContext* Current() noexcept;

    private:
        CallContext m_context;
        const CallContext* m_previous;
    };

    // PrsmScriptHost::Initialize 가 자기 스레드를 게임 스레드로 묶는다.
    void BindGameThread() noexcept;
    bool OnGameThread() noexcept;

    // 코루틴 폴링 중에는 호출 하나가 어느 컴포넌트의 것인지 실행 중인 태스크가 정한다.
    // 그 태스크 소유 컴포넌트의 엔티티 핸들(없으면 0). PrsmScriptHost 가 구현한다.
    std::uint64_t CurrentTaskOwnerHandle() noexcept;

    // 핸들 → 살아 있는 엔티티. 낡은 세대·빈 슬롯·0 은 nullptr.
    Entity* ResolveEntity(std::uint64_t handle) noexcept;
} // namespace prsm_engine
