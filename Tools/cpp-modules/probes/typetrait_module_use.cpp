// 관찰 항목 — ce.probe.typetrait 의 컴파일타임 표면을 import 로 쓴다.
//
// 링크에 기대지 않는 것만 쓴다(consteval 타입 이름 · FNV-1a). 모듈이 서기만
// 하면 초록이어야 한다. 컴파일만 한다(/c).
#include <cstddef>
#include <string_view>

import ce.probe.typetrait;

static_assert(fnv1a_64("CreatorEngine") != 0, "typetrait/fnv — constexpr 해시가 import 로 돈다");

int cpp_module_typetrait_use()
{
	constexpr std::string_view name = TypeTrait::type_name<int>();
	constexpr HashedGuid id = TypeTrait::MakeTypeID<int>();
	static_assert(name == "int", "typetrait/name — 타입 이름이 같다");
	return id == HashedGuid{} ? 1 : 0;
}
