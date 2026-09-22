#pragma once
// PHASE 14 P1 — 컴파일 타임 마커.
//
// 이벤트마다 문자열을 복사하지 않는다. 마커 이름은 NTTP(비타입 템플릿 인자)로
// 받아 이름마다 정적 슬롯 하나가 생기고, hot path 에는 그 슬롯이 들고 있는
// 정수 id 만 흐른다. 옛 코어가 필요로 했던 프레임당 이름 예산(16,384B)과
// 누락 계수(DroppedNames)는 이 설계에서 **개념 자체가 없다**.
//
// 층 B(STL 표기, snake_case) — CodingConventions §4·§5.4.
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ce
{
	using marker_id = std::uint32_t;

	// 등록되지 않은 마커. 0 을 유효 id 로 쓰지 않으므로 "빈 마커" 를 따로
	// 표현할 수 있고, 계측이 꺼진 구성에서 스코프가 이 값을 들고 다녀도
	// 수집기가 곧바로 걸러낸다.
	inline constexpr marker_id invalid_marker = 0;

	// 마커가 속한 갈래. 지금은 CPU 하나뿐이지만 P4(GPU)·P5(counter)가
	// 같은 표를 쓰므로 자리를 먼저 둔다 — 나중에 값이 늘어도 id 는 안 바뀐다.
	enum class marker_kind : std::uint8_t
	{
		cpu_scope = 0,
		counter   = 1,
		instant   = 2,
		gpu_span  = 3,
	};

	struct marker_desc
	{
		const char*  name = "";
		const char*  file = nullptr;
		std::uint32_t line = 0;
		marker_kind  kind = marker_kind::cpu_scope;
	};

	namespace detail
	{
		// NTTP 로 받을 수 있는 문자열. 배열을 값으로 들고 다녀야 구조적
		// 타입(structural type) 요건을 만족한다 — 포인터로는 NTTP 가 안 된다.
		template <std::size_t N>
		struct fixed_string
		{
			char value[N]{};

			consteval fixed_string(const char (&literal)[N])
			{
				std::copy_n(literal, N, value);
			}

			constexpr const char* c_str() const { return value; }
		};

		// 등록은 여기 한 곳에서만 일어난다. registry 를 함수 지역 static 으로
		// 들고 있으므로(ProfileMarker.cpp) **TU 간 정적 초기화 순서에
		// 의존하지 않는다** — 어느 TU 의 슬롯이 먼저 깨어나든 그때 표가 선다.
		marker_id intern_marker(const marker_desc& desc);

		// 이름마다 하나씩 생기는 슬롯. inline 변수라 TU 가 몇 개든, 유니티
		// 빌드가 청크를 어떻게 묶든 링커가 한 벌로 합친다. 그래서 같은 이름은
		// 언제나 같은 id 이고, 이것이 §5.2 의 "동일 marker 가 여러 스레드에서
		// 하나의 안정된 ID" 완료조건을 **자료구조로** 만족시킨다.
		template <fixed_string Name, marker_kind Kind>
		struct marker_slot
		{
			static inline const marker_id id =
				intern_marker(marker_desc{ Name.c_str(), nullptr, 0, Kind });
		};
	}

	// 정적 마커. 같은 이름은 어디서 불러도 같은 id 다.
	//
	//     ce::profile_scope _{ ce::marker<"AnimatorSystem">() };
	template <detail::fixed_string Name, marker_kind Kind = marker_kind::cpu_scope>
	inline marker_id marker()
	{
		return detail::marker_slot<Name, Kind>::id;
	}

	// 등록된 마커를 되읽는다. reader(UI·CLI·저장)만 쓴다 — hot path 에는
	// 이 경로가 없다.
	// 런타임 이름의 마커. 이름이 컴파일 시간에 없을 때만 쓴다 — GPU 패스
	// 이름이 그렇다(그래프가 정하고 셰이더 구성에 따라 달라진다).
	//
	// ★ 표가 **이름을 소유한다.** 정적 경로는 문자열 리터럴을 가리키므로
	//   포인터만 들고 있어도 되지만, 런타임 이름은 부르는 쪽의 버퍼가 곧
	//   덮인다 — 그대로 들면 나중에 읽을 때 남의 글자가 나온다.
	marker_id intern_runtime_marker(std::string_view name, marker_kind kind);

	const marker_desc& marker_info(marker_id id);
	std::span<const marker_desc> registered_markers();
	std::uint32_t registered_marker_count();

	// ── 캡처가 들고 다니는 어휘(P6) ──────────────────────────────────────
	//
	// ★ `marker_desc` 는 **포인터**를 들고 있고 그 글자는 이 프로세스의
	//   registry 가 소유한다. 파일에서 읽은 캡처에는 그 주인이 없다 — 그
	//   프로세스는 남의 빌드가 등록한 이름을 등록한 적이 없고, 같은 id 가
	//   전혀 다른 이름을 가리킨다. 전역 표로 이름을 풀면 그때 화면이
	//   **조용히 남의 이름**을 그린다.
	//
	//   그래서 캡처는 자기 어휘를 **글자째** 들고 다닌다. 얼리는 순간 한 벌
	//   복사하고, 그 뒤로는 registry 가 얼마나 자라든 이 캡처의 뜻은 안 변한다.
	struct capture_marker
	{
		std::string   name;
		std::string   file;   // 비어 있을 수 있다
		std::uint32_t line = 0;
		marker_kind   kind = marker_kind::cpu_scope;
	};

	// 지금 등록된 표를 **글자를 복사해** 한 벌 뜬다. 첫 칸은 `invalid_marker`
	// 자리표이므로 인덱스가 곧 id 다.
	//
	// ★ `registered_markers()` 와 달리 잠금 밖으로 포인터를 내보내지 않는다.
	//   그쪽은 등록이 겹치면 재할당된 옛 저장소를 볼 수 있어 "등록이 멈춘
	//   뒤에만 쓴다" 는 약속에 기대고 있었다. 복사는 그 약속이 필요 없다.
	std::vector<capture_marker> snapshot_markers();
}
