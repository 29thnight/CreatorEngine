#pragma once
// PHASE 14 P3 — 프로파일러 창의 내부 표면.
//
// 공개 표면은 ProfilerHUD.h 의 DrawProfilerHUD() 하나뿐이다. 이 헤더는 창을
// 이루는 조각들(툴바·Frame Overview·표)이 서로를 부르기 위한 것이고, 창 밖에서
// 쓰라고 만든 것이 아니다.
//
// ★ 여기에는 자료를 접는 코드가 없다. 접는 일은 전부 코어가 한다
//   (ProfileAggregate/ProfileReader) — 그래야 완료조건을 화면 없이 잰다.
//   이 층은 이미 접힌 것을 그리기만 한다.
#include <cstdint>

#include "ProfileReader.h"

namespace editor::profiler_view
{
	// 창이 소유하는 reader. 창을 닫아도 살아 있다 — 녹화는 서비스가 들고
	// 있고 선택은 이 reader 가 들고 있으므로, 창을 여닫아도 둘 다 유지된다.
	ce::capture_reader& reader();

	// tick 을 ms 로. 창 전체가 같은 환산을 쓰도록 한 자리에 둔다.
	double ticks_to_milliseconds(ce::profile_tick ticks);

	// 스레드 이름. 캡처가 들고 있는 이름표를 slot 으로 찾는다. 없으면
	// 슬롯 번호를 낸다 — 빈 칸보다 낫다.
	const char* thread_name(const ce::capture_session* capture, std::uint16_t slot);

	// 프레임 그래프. 클릭으로 한 프레임, 끌어서 범위를 고른다.
	void draw_frame_overview();

	// 집계 표. Hierarchy 는 트리, Flat 은 평평한 목록.
	void draw_hierarchy_table();
	void draw_flat_table();
	void draw_thread_table();
}
