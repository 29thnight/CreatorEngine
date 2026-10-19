#pragma once
// reflgen PrSM 바인딩 어댑터 — 생성된 브리지(reflgen_<module>_prsm.cpp)가 include 한다.
//
// 수신자 종류마다 **범용** 정의 하나씩만 있다. 엔진 클래스를 PrSM 에 더 노출할 때
// 그 클래스 header 에 reflgen::interop("prsm") 만 달면 되고 여기는 바뀌지 않는다.
//
//   receiver("entity")    entity_access<Entity>      엔티티 세대 핸들 → 살아 있는 Entity
//   receiver("component") component_access<T>        같은 엔티티 핸들 → 그 엔티티의 T 컴포넌트
//                         (T 는 Component 파생 전부, 호출마다 GetComponent<T> — 캐시 없음)
//
// 생성된 C 래퍼는 호출마다 접근 객체를 만들고 status 가 0 일 때만 get() 의 객체를 부른다.
// 여기서 검사하는 것:
//   1. 게임 스레드인가            → 아니면 WRONG_THREAD
//   2. 엔진이 연 PrSM 호출 안인가 → 아니면 INVALID_CONTEXT
//   3. 핸들이 살아 있는 엔티티인가, 컴포넌트면 그 엔티티에 T 가 있는가 → 아니면 INVALID_HANDLE
//
// 수명: 엔티티·컴포넌트 파괴는 Scene::FlushPendingDestroy 로 미뤄지고, 그 단계는 스크립트
// 호출 도중에 돌지 않는다. 그래서 검사를 통과한 대상은 이 접근 객체가 사는 동안(래퍼의 호출과
// 결과 변환 동안) 유효하다. 스크립트가 핸들을 들고 있어도 수명은 늘지 않는다.
#include <reflgen/prsm/access.h>

#include <concepts>

#include "Component.h"
#include "Entity.h"
#include "PrsmBindingContext.h"

namespace prsm_engine
{
    inline std::int32_t CheckCall() noexcept
    {
        if (!OnGameThread())
        {
            return REFLGEN_PRSM_BINDING_WRONG_THREAD;
        }
        if (nullptr == CallScope::Current())
        {
            return REFLGEN_PRSM_BINDING_INVALID_CONTEXT;
        }
        return REFLGEN_PRSM_BINDING_OK;
    }

    // 지금 호출을 연 PrSM 컴포넌트의 엔티티 핸들. 코루틴 폴링 중에는 실행 중인 태스크가 정한다.
    inline std::uint64_t CurrentOwnerHandle() noexcept
    {
        const std::uint64_t handle = CallScope::Current()->ownerHandle;
        return 0 != handle ? handle : CurrentTaskOwnerHandle();
    }

    // 접근 객체 공용 틀: 검사 결과와 빌린 포인터만 든다.
    template<class T>
    class BorrowedAccess
    {
    public:
        BorrowedAccess(const BorrowedAccess&) = delete;
        BorrowedAccess& operator=(const BorrowedAccess&) = delete;
        ~BorrowedAccess() noexcept = default;

        std::int32_t status() const noexcept { return m_status; }
        T* get() noexcept { return m_object; }

    protected:
        BorrowedAccess() noexcept = default;
        std::int32_t m_status = REFLGEN_PRSM_BINDING_OK;
        T* m_object = nullptr;
    };
} // namespace prsm_engine

namespace reflgen::prsm
{
    template<class T>
        requires std::same_as<T, Entity>
    class entity_access<T> : public ::prsm_engine::BorrowedAccess<T>
    {
    public:
        explicit entity_access(std::uint64_t handle) noexcept
        {
            this->m_status = ::prsm_engine::CheckCall();
            if (REFLGEN_PRSM_BINDING_OK != this->m_status)
            {
                return;
            }
            this->m_object = ::prsm_engine::ResolveEntity(handle);
            if (nullptr == this->m_object)
            {
                this->m_status = REFLGEN_PRSM_BINDING_INVALID_HANDLE;
            }
        }
    };

    template<class T>
        requires std::same_as<T, Entity>
    class entity_owner<T>
    {
    public:
        static std::int32_t get(std::uint64_t* output) noexcept
        {
            const std::int32_t status = ::prsm_engine::CheckCall();
            if (REFLGEN_PRSM_BINDING_OK != status)
            {
                return status;
            }
            *output = ::prsm_engine::CurrentOwnerHandle();
            return REFLGEN_PRSM_BINDING_OK;
        }
    };

    template<class T>
        requires std::derived_from<T, Component>
    class component_access<T> : public ::prsm_engine::BorrowedAccess<T>
    {
    public:
        explicit component_access(std::uint64_t handle) noexcept
        {
            this->m_status = ::prsm_engine::CheckCall();
            if (REFLGEN_PRSM_BINDING_OK != this->m_status)
            {
                return;
            }
            Entity* entity = ::prsm_engine::ResolveEntity(handle);
            this->m_object = nullptr != entity ? entity->GetComponent<T>() : nullptr;
            if (nullptr == this->m_object)
            {
                this->m_status = REFLGEN_PRSM_BINDING_INVALID_HANDLE;
            }
        }
    };

    // 컴포넌트 진입은 소유 엔티티의 핸들을 준다. 그 엔티티에 T 가 없으면 생성된 래퍼가
    // 같은 handle 로 component_access 를 만들어 INVALID_HANDLE 로 실패한다.
    template<class T>
        requires std::derived_from<T, Component>
    class component_owner<T>
    {
    public:
        static std::int32_t get(std::uint64_t* output) noexcept
        {
            const std::int32_t status = ::prsm_engine::CheckCall();
            if (REFLGEN_PRSM_BINDING_OK != status)
            {
                return status;
            }
            *output = ::prsm_engine::CurrentOwnerHandle();
            return REFLGEN_PRSM_BINDING_OK;
        }
    };
} // namespace reflgen::prsm
