#pragma once
// PHASE 14 P2 — 이벤트 레코드와 writer 전용 청크.
//
// ★ 이 파일이 §3.1 의 원죄를 끊는 자리다. 옛 코어는 수집기가 producer 의
//   TLS 를 직접 만졌다(`Tick()` 의 `pTLS->NumEvents = 0`). 그래서 스레드가
//   죽으면 UAF, 표 순회에 락이 필요하고, 락을 넓히면 교착이고, 수집 시점에
//   스팬을 정렬해야 해서 부등호 하나로 스레드가 통째로 사라졌다. 결함 8종 중
//   다섯이 그 하나의 파생이었다.
//
//   여기서는 writer 가 자기 청크를 **봉인해서 넘긴다**. 봉인된 뒤에는 writer
//   가 그 청크를 두 번 다시 만지지 않고, 수집기는 봉인된 것만 만진다. 두
//   소유자가 같은 메모리를 동시에 보는 순간이 존재하지 않으므로, 그 다섯은
//   고쳐지는 것이 아니라 **발생할 수 없게** 된다.
#include <atomic>
#include <cstdint>

#include "ProfileMarker.h"

namespace ce
{
	using profile_tick = std::uint64_t;

	enum class event_flags : std::uint8_t
	{
		none = 0,

		// 이 구간이 시작을 못 본 채 끝났다 — 녹화가 도중에 시작됐다는 뜻이다.
		// 분석기가 이것을 모르면 시작 시각을 녹화 시점으로 읽어 self time 을
		// 통째로 틀리게 만든다(§6.1).
		truncated_begin = 1 << 0,

		// 이 구간이 끝을 못 본 채 프레임이 넘어갔다. 옛 코어는 이 경우를
		// 조용히 잃었고, 그것이 selftest 의 cross-frame/preserve 가 기지
		// 결함으로 남아 있던 이유다.
		truncated_end = 1 << 1,

		// GPU 큐에서 이미 끝난 구간. CPU 스코프와 달리 **늦게** 도착한다 —
		// 펜스가 완료된 뒤에야 읽을 수 있기 때문이다(실측 제출→수집 최대
		// 54.6 ms, 60 Hz 로 세 프레임이 넘는다).
		//
		// ★ 수집기는 이 표식을 보고 이벤트를 **수집한 프레임**이 아니라
		//   **자기 프레임 칸**으로 돌려보낸다. 그러지 않으면 GPU 일이 세 칸
		//   뒤에 그려지고, 그것은 "UI 보다 frame identity 가 먼저다"(§13.1)를
		//   정면으로 어긴다.
		gpu_span = 1 << 2,
	};

	inline constexpr event_flags operator|(event_flags a, event_flags b)
	{
		return static_cast<event_flags>(static_cast<std::uint8_t>(a) | static_cast<std::uint8_t>(b));
	}

	inline constexpr bool has_flag(event_flags value, event_flags probe)
	{
		return (static_cast<std::uint8_t>(value) & static_cast<std::uint8_t>(probe)) != 0;
	}

	// 한 구간. 이름이 아니라 marker_id 만 들고 다니므로 프레임마다 도는
	// 문자열 복사가 없다 — 옛 코어의 이름 예산(16,384B)과 DroppedNames 가
	// 사라지는 것이 이 한 줄의 결과다.
	struct profile_event
	{
		profile_tick  tick_begin = 0;
		profile_tick  tick_end = 0;
		marker_id     marker = invalid_marker;
		std::uint32_t frame = 0;      // 이 구간이 **시작한** 엔진 프레임

		// ★ 귀속은 이벤트가 들고 다닌다. 청크에만 두면 수집기가 이벤트를
		//   프레임 벡터로 옮기는 순간 어느 스레드의 것인지 잃는다 —
		//   그것이 옛 코어가 수집 시점에 스팬을 정렬해야 했던 이유이고,
		//   그 정렬의 부등호 하나가 스레드를 통째로 사라지게 했다.
		std::uint16_t thread_slot = 0;
		std::uint16_t depth = 0;
		event_flags   flags = event_flags::none;

		// 어느 큐의 구간인가. CPU 스코프는 0 이고, GPU 구간만 백엔드가 준
		// queue id 를 싣는다. §7.3 의 여섯째 트랙(Compute/Copy)이 설 때
		// 레인을 가르는 자가 이것이다.
		std::uint8_t  queue = 0;

		// ── GPU 구간만 쓰는 칸 ────────────────────────────────────────────
		//
		// ★ CPU 스코프에서는 0 이다. 그런데도 모든 이벤트가 이 여섯 바이트를
		//   지고 간다 — 자리를 나누는(union) 대신 그냥 넓혔다. 레코드가
		//   32 → 40 바이트(+25%)가 되고, 600 프레임 실측 캡처의 총량은
		//   863 KB 다(예산 128 MB). 자리를 겹쳐 두면 "지금 이 칸이 무슨
		//   뜻인가" 를 flags 로 매번 되물어야 하고, 그 물음을 한 번 빠뜨리면
		//   CPU 구간의 depth 가 제출 번호로 읽힌다.
		//
		// ★ fence 는 싣지 않는다. 이 백엔드에서 fence 값은 제출과 1:1 이라
		//   이벤트마다 8 바이트를 더 지고 **같은 것에 두 번째 이름**을 주는
		//   일이 된다. 큐가 여럿이 되어 둘이 갈라지면 그때 싣는다.
		std::uint32_t submission = 0;   // 이 구간이 실린 GPU 제출 번호
		std::uint16_t view = 0;         // 어느 뷰(카메라)의 제출인가
		std::uint16_t reserved = 0;
	};

	// ★ 이 수를 **못 박아 둔다.** 링의 메모리는 이 레코드 × 보존 프레임의
	//   이벤트 수이고, 여기에 필드를 하나 더하는 일은 캡처 전체의 크기를
	//   바꾸는 결정이다. 조용히 커지면 아무도 그 결정을 내린 적이 없게 된다 —
	//   늘려야 한다면 이 줄을 함께 고치고, 왜 늘렸는지를 위에 적어라.
	static_assert(sizeof(profile_event) == 40,
	              "profile_event 의 크기가 바뀌었다 — 링 메모리가 그만큼 움직인다");

	// GPU 구간이 들고 오는 귀속. 어느 제출의, 어느 뷰의, 어느 큐의 것인가.
	//
	// ★ 인자를 셋 더 늘리는 대신 묶었다. `write_span(id, begin, end, frame,
	//   depth, submission, view, queue)` 는 같은 폭의 정수가 줄줄이 서서,
	//   자리를 하나 바꿔 넣어도 컴파일러가 아무 말도 하지 않는다.
	struct gpu_span_context
	{
		std::uint32_t submission = 0;
		std::uint16_t view = 0;
		std::uint8_t  queue = 0;
	};

	// 청크 하나의 이벤트 수. 프레임당 27~38 개가 현재 실측이므로 256 이면
	// 게임 스레드는 여러 프레임에 한 번 봉인한다. 상수는 크게 잡아 문제를
	// 숨기지 않는다(§6.2) — 부족하면 drop 이 아니라 봉인 빈도로 먼저 나타난다.
	inline constexpr std::uint32_t kEventsPerChunk = 256;

	// writer 전용 저장소. 봉인 전에는 오직 자기 스레드만, 봉인 뒤에는 오직
	// 수집기만 만진다. 두 시기가 겹치지 않는다는 것이 이 타입의 계약 전부다.
	struct event_chunk
	{
		profile_event events[kEventsPerChunk]{};
		std::uint32_t count = 0;

		// 이 청크를 쓴 스레드의 슬롯. 봉인 뒤 수집기가 귀속을 읽는 유일한
		// 근거다 — 옛 코어처럼 수집 시점에 스팬을 정렬해 맞추지 않는다.
		std::uint32_t thread_slot = 0;

		// 같은 타임스탬프를 가진 이벤트의 순서를 스레드 안에서 보존한다(§6.2).
		std::uint64_t sequence = 0;

		event_chunk* next = nullptr;   // pool 과 sealed 목록이 함께 쓰는 고리

		// 늦게 오는 이벤트를 담은 청크인가. 수집기가 청크 단위로 가르므로
		// 이벤트마다 표식을 보지 않아도 된다 — CPU 경로는 지금처럼 통째로 잇는다.
		bool late_ingest = false;

		// 이 청크를 쓴 녹화 세대. Clear 는 세대를 올리므로, 그 전에 열린
		// 청크가 나중에 도착하면 세대가 어긋나고 수집기가 버린다.
		//
		// ★ 이것이 없으면 잠든 워커가 Clear **전에** 적은 것을 들고 깨어나
		//   새 녹화에 섞는다. 지운 것이 돌아오는 셈이다.
		std::uint64_t generation = 0;

		void reset(std::uint32_t slot, std::uint64_t seq)
		{
			count = 0;
			late_ingest = false;
			generation = 0;
			thread_slot = slot;
			sequence = seq;
			next = nullptr;
		}

		bool full() const { return count >= kEventsPerChunk; }
	};
}
