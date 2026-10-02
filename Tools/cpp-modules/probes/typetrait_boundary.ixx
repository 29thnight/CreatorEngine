// 관찰 항목 — TypeTrait.h 를 모듈 뒤로 숨길 수 있는가.
//
// TypeTrait.h 는 239 TU 가 전이로 닿는 공용 헤더지만 ce.core 에 넣지 않았다.
// 모듈 경계를 넘지 못하는 것이 셋 있기 때문이다. 이 모듈은 그 셋이 실제로
// 어떻게 드러나는지를 v145 에서 재기 위한 표본이다 — 제품 코드가 아니다.
//
//   ① 매크로   — type_guid(T) · make_guid(). 이름 있는 모듈은 매크로를 내보내지
//                않는다. typetrait_macro_boundary.cpp 가 그것을 실패로 확인한다.
//   ② Win32    — combaseapi.h 의 GUID · HRESULT · CoCreateGuid · FAILED.
//                windows.h 를 전역 모듈 조각에 넣어도 매크로는 소비자에게 가지 않는다.
//   ③ 내부 연결 — `static std::set<HashedGuid> g_guids;` 와
//                `static inline FileGuid nullFileGuid`. 헤더에 둔 내부 연결
//                전역이라 번역 단위마다 사본이 생기고, GUIDCreator 의 inline
//                함수가 그 사본을 만진다. 모듈 인터페이스에서 이런 TU-local
//                엔터티를 드러내는 것(exposure)은 표준상 금지다. 이 모듈의 컴파일
//                자체가 붉을 수 있고, 그것이 관찰 결과다.
module;

#include <windows.h>
#include "TypeTrait.h"

export module ce.probe.typetrait;

export using ::HashedGuid;
export using ::FileGuid;
export using ::fnv1a_64;

export namespace TypeTrait
{
	using ::TypeTrait::type_name;
	using ::TypeTrait::MakeTypeID;
	using ::TypeTrait::GUIDCreator;
}
