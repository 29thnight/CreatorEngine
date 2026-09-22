#pragma once
// PHASE 14 P1 — 계측 표기.
//
// 호출부가 보는 것은 이 파일뿐이다. 매크로는 없다(§5.2).
//
//     ce::profile_scope _{ ce::marker<"AnimatorSystem">() };
//
// 여는 일과 닫는 일이 한 문장이므로 **짝 불균형을 작성할 수 없다**. 옛 코어의
// 결함 6(EndEvent 가 건너뛰어져 깊이가 영구히 어긋나고, 32 를 넘으면 TLS 의
// 다음 멤버를 덮어쓴다)은 고쳐진 것이 아니라 표현할 수 없게 됐다.
//
// CE_SHIPPING 구성에서는 스코프가 빈 껍데기가 되고 마커 등록도 일어나지
// 않는다 — 새 매크로를 만들지 않고 Directory.Build.targets 가 이미 전
// 프로젝트에 정의하는 것을 문다(§5.2 "만들 것이 아니라 물 것이다").
#include "ProfileMarker.h"
#include "ProfileService.h"

namespace ce
{
	// 엔진이 쓰는 라이브 서비스. 검사용 서비스는 자기 인스턴스를 따로 세운다.
	profiler_service& profiler();

// ★ `defined(CE_SHIPPING)` 이 아니라 **값**을 본다. Directory.Build.targets 는
//   두 구성 모두에서 이 매크로를 정의하고 값으로만 가른다
//   (`CE_SHIPPING=1;CE_DEVELOPMENT=0` 또는 `CE_SHIPPING=0;CE_DEVELOPMENT=1`).
//   그래서 defined() 로 물으면 Development 에서도 참이 되어 계측이 통째로
//   빈 껍데기가 된다 — 실제로 그렇게 썼다가 에디터의 이벤트가 0 이 됐고,
//   "수집은 도는데 이벤트만 0" 이라는 모양으로 게이트가 잡았다.
#if CE_SHIPPING

	class profile_scope
	{
	public:
		explicit profile_scope(marker_id) {}
		profile_scope(profiler_service&, marker_id) {}
		profile_scope(const profile_scope&) = delete;
		profile_scope& operator=(const profile_scope&) = delete;
	};

	inline void profile_scope_begin(marker_id) {}
	inline void profile_scope_end() {}
	inline void profile_instant(marker_id) {}

#else

	class profile_scope
	{
	public:
		explicit profile_scope(marker_id id)
			: profile_scope(profiler(), id)
		{
		}

		// ★ 서비스를 받는 판. 이것이 없으면 스코프가 전역 하나에만 찍혀
		//   인스턴스로 세운 서비스에는 아무것도 들어가지 않는다 — 검사용
		//   서비스를 세울 수 있게 만든 설계가 반쪽이 된다. 실제로 코어
		//   프로브가 이 결함을 먼저 잡았다.
		profile_scope(profiler_service& service, marker_id id)
			: m_service(&service)
		{
			m_service->begin_scope(id);
		}

		~profile_scope()
		{
			m_service->end_scope();
		}

		profile_scope(const profile_scope&) = delete;
		profile_scope& operator=(const profile_scope&) = delete;

	private:
		profiler_service* m_service;
	};

	// ★ 여는 일과 닫는 일이 **다른 함수로 갈리는 경계**에서만 쓴다 — 수명
	//   훅처럼 콜백이 두 개로 오는 자리다. 그 자리에서는 RAII 를 쓸 수 없지만,
	//   그렇다고 `profiler()` 를 직접 부르면 CE_SHIPPING 약속이 이 파일 밖으로
	//   샌다 — 구성으로 계측을 끄는 계약은 한 곳에서만 지켜져야 한다. 그래서
	//   껍데기 판을 함께 둔다.
	//
	//   짝은 부르는 쪽이 맞춰야 하므로, 훅을 받는 층에서 곧바로 RAII 로 다시
	//   묶어라 — 실제로 렌더 스레드는 그렇게 쓴다.
	inline void profile_scope_begin(marker_id id) { profiler().begin_scope(id); }
	inline void profile_scope_end() { profiler().end_scope(); }

	// 길이가 없는 사건. RAII 로 묶을 짝이 없으므로 함수 하나다 —
	// 짝이 없다는 것이 이 표기의 뜻 전부다.
	inline void profile_instant(marker_id id) { profiler().mark_instant(id); }

#endif
}
