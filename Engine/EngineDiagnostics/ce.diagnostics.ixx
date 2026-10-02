// C++ 모듈 전환 1단계 — EngineDiagnostics 의 모듈 입구(파일럿).
//
// ★ 이 파일은 아직 EngineDiagnostics.vcxproj 에 들어 있지 않다. 솔루션 빌드는
//   이 파일을 모른다. 컴파일하는 것은 Tools/cpp-modules/Invoke-CppModuleProbe.ps1
//   하나뿐이고, 그 검사가 초록이 된 뒤에 프로젝트에 넣는다.
//
// ── 왜 정의를 옮기지 않고 헤더를 감싸는가 ──
//
//   헤더 소비자(44 곳이 ProfileScope.h 를 연다)와 모듈 소비자가 한동안 같은
//   프로그램 안에 함께 산다. 같은 타입을 모듈 본문(purview)에 다시 정의하면
//   그 정의는 이름 있는 모듈에 붙어 **헤더 쪽 정의와 다른 엔터티**가 된다.
//   그러면 다음 둘이 조용히 둘로 갈린다.
//
//     · `inline thread_local current_cpu_context` (ProfileScope.h)
//       — 모듈 쪽에서 연 profile_context_scope 를 헤더 쪽 스코프가 못 본다.
//         CPU 귀속이 경고 없이 어긋난다.
//     · `detail::marker_slot<Name, Kind>::id` (ProfileMarker.h)
//       — 같은 이름의 슬롯이 둘 생긴다. 지금은 intern_marker 의 이름 중복
//         제거가 이중 안전망으로 받아 주지만, 그 안전망에 기대는 설계가 된다.
//
//   그래서 헤더를 global module fragment 에서 열고 이름만 `export using` 으로
//   내보낸다. 엔터티는 전부 전역 모듈에 붙은 채로 남고, 헤더로 본 쪽과 import
//   로 본 쪽이 **같은 엔터티**를 가리킨다. 정의는 여전히 헤더 한 벌이다.
//
//   소유권을 모듈로 옮기는 일(2단계)은 한 헤더의 소비자가 전부 import 로 바뀐
//   뒤에만 한다.
//
// ── 내보내는 표면 ──
//
//   헤더가 지금 보여 주는 ce:: 이름을 **그대로** 내보낸다. 1단계에서 공개
//   API 를 다시 설계하지 않는다 — 표면이 달라지면 "모듈이라서 깨졌다" 와
//   "API 가 바뀌어서 깨졌다" 를 가를 수 없다. detail:: 은 내보내지 않는다.
//   `ce::marker<"Name">()` 의 NTTP 타입(detail::fixed_string)은 소비자가
//   이름으로 부르지 않으므로 도달 가능(reachable)이기만 하면 된다.
//
// ── Development / Shipping ──
//
//   ProfileMarker.h 의 `marker()` 와 ProfileScope.h 의 스코프 전체가 CE_SHIPPING
//   **값**으로 갈린다. BMI 는 그 값을 굳혀서 담으므로, 구성마다 따로 만들어야
//   한다(MSBuild 는 IntDir 이 EngineConfigKey 로 갈려 이미 그렇게 된다).
//   어긋난 BMI 를 다른 구성의 소비자가 import 하면 컴파일러는 아무 말도 하지
//   않는다 — 사용자 매크로는 BMI 호환성 검사 대상이 아니다. 그래서 BMI 가 자기
//   구성을 `ce::diagnostics_build::shipping` 으로 들고 다니고, 소비자가
//   static_assert 로 대조한다(Tools/cpp-modules/probes 참고).
module;

// ★ 정의되지 않은 CE_SHIPPING 은 `#if` 에서 조용히 0 이 된다. 헤더 빌드에서는
//   Directory.Build.targets 가 항상 정의하지만, BMI 는 구성을 굳혀 담는 산출물
//   이므로 "정의되지 않아서 Development" 를 여기서 받아 주지 않는다.
#if !defined(CE_SHIPPING)
#error "ce.diagnostics: CE_SHIPPING 이 정의되지 않았다 — BMI 는 구성마다 따로 만든다 (CE_SHIPPING=0 또는 1)"
#endif

#include "ProfileMarker.h"
#include "ProfileEvent.h"
#include "ProfileThreadStream.h"
#include "ProfileCapture.h"
#include "ProfileAggregate.h"
#include "ProfileCaptureFile.h"
#include "ProfileReader.h"
#include "ProfileService.h"
#include "ProfileScope.h"

export module ce.diagnostics;

export namespace ce
{
	// ── ProfileMarker.h ──────────────────────────────────────────────────
	using ce::marker_id;
	using ce::invalid_marker;
	using ce::marker_kind;
	using ce::marker_desc;
	using ce::marker;
	using ce::intern_runtime_marker;
	using ce::marker_info;
	using ce::registered_markers;
	using ce::registered_marker_count;
	using ce::capture_marker;
	using ce::snapshot_markers;

	// ── ProfileEvent.h ───────────────────────────────────────────────────
	using ce::profile_tick;
	using ce::event_flags;
	// ★ 연산자도 이름이다. 내보내지 않으면 import 쪽에서 `a | b` 가 ADL 로도
	//   안 보인다 — 전역 모듈 조각의 선언은 내보낸 것만 소비자에게 보인다.
	using ce::operator|;
	using ce::has_flag;
	using ce::cpu_span_context;
	using ce::profile_event;
	using ce::gpu_span_context;
	using ce::kEventsPerChunk;
	using ce::kProfilePageMagic;
	using ce::kProfilePageVersion;
	using ce::event_chunk;

	// ── ProfileThreadStream.h ────────────────────────────────────────────
	using ce::track_kind;
	using ce::thread_info;
	using ce::track_precedes;
	using ce::chunk_pool;
	using ce::open_scope;
	using ce::kMaxScopeDepth;
	using ce::thread_stream;

	// ── ProfileCapture.h ─────────────────────────────────────────────────
	using ce::profile_counter_id;
	using ce::counter_category;
	using ce::counter_mask;
	using ce::counter_bit;
	using ce::capture_counter;
	using ce::register_counter;
	using ce::snapshot_counters;
	using ce::counter_category_bit;
	using ce::find_counter;
	using ce::profile_counter_sample;
	using ce::frame_events;
	using ce::frame_record;
	using ce::kDefaultRetainedFrames;
	using ce::kDefaultMemoryBudget;
	using ce::capture_environment;
	using ce::capture_session;
	using ce::capture_session_ptr;
	using ce::capture_ring;

	// ── ProfileAggregate.h ───────────────────────────────────────────────
	using ce::aggregate_row;
	using ce::thread_summary;
	using ce::frame_boundary;
	using ce::aggregate_scope;
	using ce::frame_aggregate;
	using ce::aggregate_frames;

	// ── ProfileCaptureFile.h ─────────────────────────────────────────────
	using ce::kCaptureFileVersion;
	using ce::capture_file_error;
	using ce::describe;
	using ce::encode_capture;
	using ce::decode_capture;
	using ce::save_capture;
	using ce::load_capture;

	// ── ProfileReader.h ──────────────────────────────────────────────────
	using ce::capture_reader;

	// ── ProfileService.h ─────────────────────────────────────────────────
	using ce::recorder_state;
	using ce::profiler_config;
	using ce::collector_timing;
	using ce::live_summary;
	using ce::kMaxLiveServices;
	using ce::profiler_service;

	// ── ProfileScope.h ───────────────────────────────────────────────────
	using ce::profiler;
	using ce::profile_context_scope;
	using ce::profile_scope;
	using ce::profile_scope_begin;
	using ce::profile_scope_end;
	using ce::profile_instant;
#if !CE_SHIPPING
	// Shipping 에서는 이 변수 자체가 없다(스코프가 빈 껍데기다).
	using ce::current_cpu_context;
#endif
}

// 이 BMI 가 어느 구성으로 만들어졌는가. 모듈에 새로 붙는 엔터티는 이것
// 하나뿐이다 — 헤더 쪽에 같은 이름이 없으므로 중복 정의 문제가 없다.
export namespace ce::diagnostics_build
{
	inline constexpr bool shipping = (CE_SHIPPING != 0);
}
