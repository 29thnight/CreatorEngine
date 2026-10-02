// C++ 모듈 전환 — Utility_Framework 공용층의 모듈 입구(파일럿).
//
// ★ 이 파일은 아직 Utility_Framework.vcxproj 에 들어 있지 않다. 컴파일하는 것은
//   Tools/cpp-modules/Invoke-CppModuleProbe.ps1 하나뿐이다.
//
// ── 왜 이 두 헤더부터인가 ──
//
//   빌드 시간의 몸통은 Core.Minimal.h → Core.Definition.h 사슬이다(windows.h ·
//   DXGI · d3dcompiler · comdef · STL 26 개 · plf_colony). 그런데 그 사슬은 지금
//   모듈 뒤로 숨길 수 없다 — **이름 있는 모듈은 매크로를 내보내지 않는다.**
//   FAILED/SUCCEEDED, UNICODE 에 따라 이름이 바뀌는 Win32 API, type_guid(T) 같은
//   것에 기대는 소비자가 import 로 바꾸는 순간 깨진다. Windows/DX/COM 층을
//   먼저 떼어 내는 것이 순서다.
//
//   여기 담은 둘은 그 사슬과 무관하고 STL 만 쓴다.
//     · Uuid.h          — 302 TU 가 전이로 닿는 가장 넓은 공용 헤더. 헤더 온리.
//     · ClassProperty.h — Singleton<T>. 정적 데이터 멤버(s_instance)가 헤더
//                         소비자와 모듈 소비자 사이에서 **하나**로 남는지를
//                         재기에 좋은 표본이다.
//
//   TypeTrait.h(239 TU)는 넣지 않았다. combaseapi.h 를 열고, 매크로(type_guid ·
//   make_guid)를 정의하고, 헤더 안에 내부 연결 전역(`static std::set<HashedGuid>
//   g_guids`, `static inline FileGuid nullFileGuid`)을 둔다 — 셋 다 모듈 경계를
//   넘지 못한다. 그 경계는 Tools/cpp-modules/probes/typetrait_boundary.ixx 가
//   관찰 항목으로 따로 잰다.
//
// ── 정의를 옮기지 않는 이유 ──
//
//   ce.diagnostics.ixx 머리말과 같다. 헤더를 global module fragment 에서 열고
//   이름만 내보내므로, 헤더로 본 쪽과 import 로 본 쪽이 같은 엔터티를 본다.
module;

#include "Uuid.h"
#include "ClassProperty.h"

export module ce.core;

export namespace Uuid
{
	using ::Uuid::Sha1;
	// operator== · operator<=> 는 숨은 friend 라 ADL 로 따라온다 — 따로
	// 내보낼 이름이 없다.
	using ::Uuid::Uuid16;
	using ::Uuid::Nil;
	using ::Uuid::ToString;
	using ::Uuid::FromName;
	using ::Uuid::TryParse;
	using ::Uuid::Parse;
}

export using ::Noncopyable;
export using ::Singleton;
