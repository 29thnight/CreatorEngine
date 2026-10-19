// PrSM 호스트 API(ce_*) — 생성된 스크립트 모듈이 import 하는 엔진 쪽 구현.
//
// 계약은 ThirdParty/PrSM/abi/prsm_abi.h 가 원본이고, 의미는 PrSM 하네스의 ce_api.cpp 와
// 같다. 다른 점은 로그가 엔진 로그로 간다는 것뿐이다. C++ 예외는 C ABI 를 넘지 않는다:
// 호스트 API 오용(잘못된 서술자·잘못된 스레드)은 복구할 스크립트 상태가 아니라 호스트
// 결함이므로 기록하고 중단한다.
#include "gc_runtime.h"
#include "handle_table.h"
#include "prsm_abi.h"

#include "LogSystem.h"

#include <cstdlib>
#include <exception>
#include <string>

namespace
{
    std::string Text(CeStr value)
    {
        return (nullptr != value.ptr && 0 != value.len) ? std::string(value.ptr, value.len) : std::string{};
    }

    template<class F>
    auto NoThrow(F fn) noexcept -> decltype(fn())
    {
        try
        {
            return fn();
        }
        catch (const std::exception& error)
        {
            Debug::PrintLog(spdlog::level::critical, std::string("[PrSM] 호스트 계약 위반: ") + error.what());
        }
        catch (...)
        {
            Debug::PrintLog(spdlog::level::critical, "[PrSM] 호스트 계약 위반: 알 수 없는 예외");
        }
        std::abort();
    }
} // namespace

extern "C"
{
    CE_API void* ce_gc_alloc(const PrsmTypeDesc* desc)
    {
        return NoThrow([&] { return prsm::host::GcRuntime::Instance().Allocate(desc); });
    }
    CE_API void* ce_gc_alloc_bytes(size_t size)
    {
        return NoThrow([&] { return prsm::host::GcRuntime::Instance().AllocateBytes(size); });
    }
    CE_API void ce_gc_store(void** slot, void* value)
    {
        NoThrow([&] { prsm::host::GcRuntime::Instance().Store(slot, value); });
    }
    CE_API void ce_gc_regray(void* frame)
    {
        NoThrow([&] { prsm::host::GcRuntime::Instance().Regray(frame); });
    }
    CE_API CeRoot ce_gc_root_create(void* object)
    {
        return NoThrow([&] { return prsm::host::GcRuntime::Instance().RootCreate(object); });
    }
    CE_API void ce_gc_root_destroy(CeRoot root)
    {
        NoThrow([&] { prsm::host::GcRuntime::Instance().RootDestroy(root); });
    }
    CE_API void ce_gc_trace_ref(void* tracer, void* object)
    {
        NoThrow([&] { prsm::host::GcRuntime::Instance().TraceRef(tracer, object); });
    }
    CE_API int ce_handle_is_alive(CeHandle h)
    {
        return NoThrow([&] { return prsm::host::HandleTable::Instance().IsAlive(h) ? 1 : 0; });
    }
    CE_API void* ce_handle_script_instance(CeHandle h)
    {
        return NoThrow([&] { return prsm::host::HandleTable::Instance().Resolve(h); });
    }

    CE_API PrsmCallStatus ce_report_failure(CeStr message, CeStr file, uint32_t line)
    {
        // 로그는 실패를 전파하지 못한다. 실패 상태는 생성 코드가 그대로 돌려준다.
        try
        {
            Debug::PrintLog(spdlog::level::err, "[PrSM] 스크립트 실패: " + Text(message) + " (" + Text(file) + ":"
                + std::to_string(line) + ")");
        }
        catch (...)
        {
        }
        return PRSM_CALL_FAILED;
    }

    [[noreturn]] CE_API void ce_panic(CeStr message, CeStr file, uint32_t line)
    {
        ce_report_failure(message, file, line);
        // 임의의 네이티브 콜백은 안전하게 되감을 수 없다. checked 경로는 PRSM_CALL_FAILED /
        // PRSM_TASK_PANICKED 를 돌려 컴포넌트 단위로 격리된다.
        std::abort();
    }

    CE_API void ce_log(uint32_t level, CeStr message)
    {
        try
        {
            const auto severity = level >= 2 ? spdlog::level::err
                : level == 1                 ? spdlog::level::warn
                                             : spdlog::level::info;
            Debug::PrintLog(severity, "[PrSM] " + Text(message));
        }
        catch (...)
        {
        }
    }
}
