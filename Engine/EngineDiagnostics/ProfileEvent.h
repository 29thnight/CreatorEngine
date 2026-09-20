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
		std::uint8_t  reserved = 0;
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

		void reset(std::uint32_t slot, std::uint64_t seq)
		{
			count = 0;
			thread_slot = slot;
			sequence = seq;
			next = nullptr;
		}

		bool full() const { return count >= kEventsPerChunk; }
	};
}
