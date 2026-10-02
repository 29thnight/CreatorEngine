// 헤더로만 EngineDiagnostics 를 보는 번역 단위 — 지금의 44 개 소비자와 같은 자리.
//
// 모듈 쪽 번역 단위(diagnostics_module_probe.cpp)와 한 실행 파일로 링크되어,
// 두 쪽이 같은 엔터티를 보는지를 값과 주소로 대조할 수 있게 한다.
#include <cstdint>
#include <typeinfo>

#include "ProfileMarker.h"
#include "ProfileScope.h"
#include "ProfileService.h"

#include "diagnostics_peer.h"

namespace probe::header_peer
{
	ce::marker_id shared_marker()
	{
		return ce::marker<"CppModuleProbe.Shared">();
	}

	const ce::profiler_service* global_profiler()
	{
		return &ce::profiler();
	}

	const void* current_cpu_context_address()
	{
#if CE_SHIPPING
		return nullptr;
#else
		return &ce::current_cpu_context;
#endif
	}

	std::uint64_t current_cpu_session()
	{
#if CE_SHIPPING
		return 0;
#else
		return ce::current_cpu_context.session;
#endif
	}

	bool shipping()
	{
		return CE_SHIPPING != 0;
	}

	const std::type_info& profile_event_type()
	{
		return typeid(ce::profile_event);
	}

	const std::type_info& profiler_service_type()
	{
		return typeid(ce::profiler_service);
	}

	void record_header_scope(ce::profiler_service& service)
	{
		ce::profile_scope inner{ service, ce::marker<"CppModuleProbe.HeaderScope">() };
	}
}
