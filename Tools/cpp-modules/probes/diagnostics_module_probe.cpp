// C++ 모듈 전환 1단계 — ce.diagnostics 를 **import 로만** 보는 번역 단위.
//
// 같은 실행 파일에 헤더로만 보는 번역 단위(diagnostics_header_peer.cpp)와
// EngineDiagnostics 의 .cpp 일곱 개가 함께 링크된다. 판정하는 것은 셋이다.
//
//   ① 표면   — 헤더가 보여 주던 이름이 import 만으로 전부 쓸 수 있는가.
//              `ce::marker<"Name">()` 의 NTTP(detail::fixed_string)처럼 소비자가
//              이름으로 부르지 않는 것이 도달 가능한가.
//   ② 동일성 — 헤더 쪽과 모듈 쪽이 **같은 엔터티**를 보는가. 타입(typeid),
//              전역 서비스 주소, 마커 id, 그리고 결정적인 것 — thread_local
//              current_cpu_context 의 주소와 값.
//   ③ 구성   — Development/Shipping 이 BMI 에 굳어 있고 소비자 매크로와
//              맞물리는가(diagnostics_build_guard.h).
//
// 판정은 종료 코드와 표식 CPP_MODULE_DIAG_OK 다.

// ★ STL 은 import **앞에서** 연다. 이름 있는 모듈의 전역 모듈 조각에 든 선언은
//   내보낸 것만 보인다 — std::string 의 operator== 처럼 모듈이 내보내지 않은 것을
//   쓰려면 소비자가 스스로 열어야 한다. 지금의 소비자도 그렇게 쓴다.
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <span>
#include <string>
#include <type_traits>
#include <typeinfo>
#include <vector>

#include "probe_check.h"

import ce.diagnostics;

#include "diagnostics_build_guard.h"
#include "diagnostics_peer.h"

namespace
{
	using probe::check;

	constexpr bool kShipping = (CE_SHIPPING != 0);

	std::size_t events_in(const ce::capture_session& capture, std::uint32_t frame)
	{
		const ce::frame_record* record = capture.find_frame(frame);
		return record ? record->events.size() : 0;
	}

	//-------------------------------------------------------------------------
	// ① 표면 — 이름이 보이고, 헤더에서와 같은 뜻인가.
	//-------------------------------------------------------------------------
	void test_surface()
	{
		static_assert(sizeof(ce::profile_event) == 64, "surface/event-size — 헤더의 단정과 같은 크기");
		static_assert(ce::kEventsPerChunk == 256, "surface/chunk — 상수가 같은 값");
		static_assert(ce::invalid_marker == 0, "surface/invalid — 0 은 빈 마커");

		// 연산자도 이름이다 — 내보내지 않았다면 여기서 컴파일이 멈춘다.
		constexpr ce::event_flags both = ce::event_flags::gpu_span | ce::event_flags::instant;
		static_assert(ce::has_flag(both, ce::event_flags::instant), "surface/flags-or — operator| 가 보인다");
		static_assert(!ce::has_flag(both, ce::event_flags::truncated_end), "surface/flags-probe");

		const ce::counter_mask mask = ce::counter_bit(ce::counter_category::gpu);
		check(mask != 0, "surface/counter-bit — 카운터 비트가 0 이 아니다");

		check(ce::describe(ce::capture_file_error::open_failed) != nullptr, "surface/describe — 오류 서술이 있다");
	}

	//-------------------------------------------------------------------------
	// ② 동일성 — 헤더 쪽과 같은 엔터티인가.
	//-------------------------------------------------------------------------
	void test_type_identity()
	{
		check(typeid(ce::profile_event) == probe::header_peer::profile_event_type(),
		      "identity/type-event — import 와 헤더가 같은 profile_event 를 본다");
		check(typeid(ce::profiler_service) == probe::header_peer::profiler_service_type(),
		      "identity/type-service — import 와 헤더가 같은 profiler_service 를 본다");
	}

	void test_global_service_identity()
	{
		check(&ce::profiler() == probe::header_peer::global_profiler(),
		      "identity/global-service — 엔진 전역 서비스가 하나다");
	}

	void test_marker_identity()
	{
		const ce::marker_id fromModule = ce::marker<"CppModuleProbe.Shared">();
		const ce::marker_id fromHeader = probe::header_peer::shared_marker();

		if constexpr (kShipping)
		{
			// Shipping 은 마커 등록까지 끊는다(ProfileMarker.h) — 양쪽 모두 빈 마커.
			check(fromModule == ce::invalid_marker, "shipping/marker — 모듈 쪽 마커가 비어 있다");
			check(fromHeader == ce::invalid_marker, "shipping/marker-header — 헤더 쪽 마커가 비어 있다");
		}
		else
		{
			check(fromModule != ce::invalid_marker, "marker/valid — 모듈 쪽에서 등록된다");
			check(fromModule == fromHeader, "marker/shared — 같은 이름은 경계를 넘어도 같은 id");
			check(std::string(ce::marker_info(fromModule).name) == "CppModuleProbe.Shared",
			      "marker/name — id 로 이름을 되찾는다");
		}
	}

	// ★ 이 파일의 핵심 검사. 정의를 모듈로 옮긴 래퍼였다면 thread_local 이 둘이
	//   되어, 모듈 쪽에서 연 문맥을 헤더 쪽 스코프가 못 본다. 마커 id 와 달리
	//   이것은 이름 중복 제거 같은 이중 안전망이 없다.
	//
	// ★ `if constexpr` 가 아니라 `#if` 다. 템플릿 밖의 `if constexpr` 는 버린 쪽도
	//   검사하므로, Shipping 에 없는 current_cpu_context 를 부르는 순간 멈춘다.
	void test_tls_identity()
	{
#if CE_SHIPPING
		check(probe::header_peer::current_cpu_context_address() == nullptr,
		      "shipping/tls — Shipping 에는 문맥 변수가 없다");
		check(std::is_empty_v<ce::profile_scope>, "shipping/scope-empty — 스코프가 빈 껍데기다");
		check(std::is_empty_v<ce::profile_context_scope>, "shipping/context-empty — 문맥 스코프도 빈 껍데기다");
		[[maybe_unused]] ce::profile_context_scope ignored{ ce::cpu_span_context{ 0x51, 2, 3 } };
		check(probe::header_peer::current_cpu_session() == 0, "shipping/tls-value — 문맥이 기록되지 않는다");
#else
		check(!std::is_empty_v<ce::profile_scope>, "tls/scope-live — Development 스코프는 서비스를 든다");
		check(static_cast<const void*>(&ce::current_cpu_context) ==
		          probe::header_peer::current_cpu_context_address(),
		      "tls/address — thread_local 문맥이 하나다");

		{
			ce::profile_context_scope context{ ce::cpu_span_context{ 0x51, 2, 3 } };
			check(probe::header_peer::current_cpu_session() == 0x51,
			      "tls/value — 모듈 쪽에서 연 문맥을 헤더 쪽이 본다");
		}
		check(probe::header_peer::current_cpu_session() == 0,
		      "tls/restore — 문맥이 닫히면 헤더 쪽에서도 사라진다");
#endif
	}

	//-------------------------------------------------------------------------
	// ③ 서비스 왕복 — 모듈 쪽 스코프 안에서 헤더 쪽 스코프를 연다.
	//-------------------------------------------------------------------------
	void check_development_capture(const ce::capture_session& capture)
	{
		check(events_in(capture, 1) == 2, "service/count — 모듈 스코프 하나, 헤더 스코프 하나");
		if (const ce::frame_record* frame = capture.find_frame(1); frame && frame->events.size() == 2)
		{
			// 안쪽(헤더 쪽)이 먼저 닫히므로 먼저 기록된다.
			check(frame->events[0].marker == ce::marker<"CppModuleProbe.HeaderScope">(),
			      "service/inner-marker — 헤더 쪽 스코프가 안쪽이다");
			check(frame->events[0].depth == 1,
			      "service/inner-depth — 깊이가 경계를 넘어 이어진다(스레드 스트림이 하나)");
			check(frame->events[1].depth == 0, "service/outer-depth — 모듈 쪽 스코프가 바깥이다");
		}

		const ce::frame_aggregate aggregate = ce::aggregate_frames(capture, 1, 1);
		check(!aggregate.hierarchy().empty(), "service/aggregate — 집계 트리가 선다");

		// 파일 왕복 — std::expected 와 std::byte 범위가 모듈 경계를 넘는다.
		const std::vector<std::byte> bytes = ce::encode_capture(capture);
		check(!bytes.empty(), "service/encode — 캡처가 바이트로 나온다");
		const std::expected<ce::capture_session_ptr, ce::capture_file_error> decoded =
			ce::decode_capture(std::span<const std::byte>(bytes.data(), bytes.size()));
		check(decoded.has_value(), "service/decode — 같은 바이트를 다시 읽는다");
		if (decoded.has_value() && *decoded)
		{
			check((*decoded)->frames().size() == capture.frames().size(),
			      "service/decode-frames — 프레임 수가 같다");
		}
	}

	void test_service_roundtrip()
	{
		ce::profiler_service service;
		service.initialize();
		service.register_thread("CppModuleProbe", ce::track_kind::game_thread);
		service.record(1);

		{
			ce::profile_scope outer{ service, ce::marker<"CppModuleProbe.ModuleScope">() };
			probe::header_peer::record_header_scope(service);
		}

		service.publish_frame(1);
		service.wait_until_idle();
		service.pause();
		service.wait_until_idle();

		const ce::capture_session_ptr capture = service.capture();
		if constexpr (kShipping)
		{
			// 캡처가 서든 말든, Shipping 스코프는 아무것도 찍지 않아야 한다.
			check(!capture || events_in(*capture, 1) == 0,
			      "shipping/events — Shipping 스코프는 아무것도 찍지 않는다");
		}
		else
		{
			check(capture != nullptr, "service/capture — 얼린 캡처가 있다");
			if (capture)
			{
				check_development_capture(*capture);
			}
		}

		service.shutdown();
	}
}

int main()
{
	check(probe::header_peer::shipping() == kShipping,
	      "config/peer — 헤더 쪽과 모듈 쪽 번역 단위가 같은 구성으로 컴파일됐다");

	test_surface();
	test_type_identity();
	test_global_service_identity();
	test_marker_identity();
	test_tls_identity();
	test_service_roundtrip();

	return probe::finish("CPP_MODULE_DIAG_OK");
}
