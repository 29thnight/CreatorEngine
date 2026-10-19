// 엔진에 링크된 PrSM 바인딩 모듈의 목록.
//
// 목록의 단위는 **네이티브 모듈**이다(타입이 아니다). 모듈 안에서 어느 클래스·멤버를
// 열지는 그 header 의 reflgen::interop("prsm") 표시가 정하고, 이 파일은 모듈이 생성한
// 표 진입점 하나만 안다. 엔진 클래스를 더 노출해도 여기는 바뀌지 않는다. 새 모듈
// (예: RenderEngine)에 ReflgenPrsm 을 켤 때만 줄 하나를 더한다.
//
// 표 함수 이름은 ce_prsm_table_<hex(reflgen 모듈 이름)> 이다(reflgen PrSM ABI v1).
#include "PrsmBindingRegistry.h"

#include <reflgen/prsm/binding_abi.h>

extern "C" const reflgen_prsm_binding_entry* REFLGEN_PRSM_BINDING_CDECL
ce_prsm_table_5363656e6552756e74696d65(std::uint32_t* count) noexcept;   // "SceneRuntime"

namespace prsm_engine
{
    void RegisterEngineBindingModules(BindingRegistry& registry)
    {
        registry.Register(&ce_prsm_table_5363656e6552756e74696d65);
    }
} // namespace prsm_engine
