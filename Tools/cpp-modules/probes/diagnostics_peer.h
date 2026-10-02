#pragma once
// 헤더로만 EngineDiagnostics 를 본 번역 단위(diagnostics_header_peer.cpp)가
// 내놓는 창.
//
// ★ ce:: 타입이 **이미 보이는 자리**에서 연다 — 헤더 쪽은 Profile*.h 뒤에서,
//   모듈 쪽은 `import ce.diagnostics;` 뒤에서. 이 파일은 진단 헤더를 스스로
//   열지 않는다. 열면 모듈 쪽 번역 단위가 헤더로도 보게 되어 "import 로만 본
//   쪽" 이라는 전제가 깨진다.
//
// ★ 시그니처에 ce:: 타입을 일부러 쓴다. 정의는 헤더로 본 번역 단위에, 호출은
//   import 로 본 번역 단위에 있으므로, 두 쪽의 ce::profiler_service 가 **같은
//   엔터티**여야 링크가 맞는다. 모듈 본문에 같은 타입을 다시 정의하는 래퍼였다면
//   여기서 링크 오류(또는 그보다 나쁜 조용한 불일치)가 난다.
#include <cstdint>
#include <typeinfo>

namespace probe::header_peer
{
	// ce::marker<"CppModuleProbe.Shared">() 를 헤더 쪽에서 부른 값.
	ce::marker_id shared_marker();

	// &ce::profiler() — 엔진 전역 서비스의 주소.
	const ce::profiler_service* global_profiler();

	// &ce::current_cpu_context. Shipping 에서는 그 변수가 없으므로 nullptr.
	const void* current_cpu_context_address();

	// ce::current_cpu_context.session. Shipping 에서는 0.
	std::uint64_t current_cpu_session();

	// 헤더 쪽 번역 단위가 본 CE_SHIPPING.
	bool shipping();

	const std::type_info& profile_event_type();
	const std::type_info& profiler_service_type();

	// 헤더 쪽에서 스코프 하나를 연다(마커 "CppModuleProbe.HeaderScope").
	// 모듈 쪽이 연 스코프 **안에서** 불리면 깊이 1 로 찍혀야 한다 — 스레드
	// 스트림과 깊이 계수가 경계를 넘어 하나라는 뜻이다.
	void record_header_scope(ce::profiler_service& service);
}
