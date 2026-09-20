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

#if defined(CE_SHIPPING)

	class profile_scope
	{
	public:
		explicit profile_scope(marker_id) {}
		profile_scope(profiler_service&, marker_id) {}
		profile_scope(const profile_scope&) = delete;
		profile_scope& operator=(const profile_scope&) = delete;
	};

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

#endif
}
