// 관찰 항목 — 모듈에 붙은 타입의 이름이 TypeTrait::type_name<T>() 에서 그대로인가.
//
// type_name<T>() 는 __FUNCSIG__ 를 잘라 이름을 얻고, MakeTypeID<T>() 가 그 이름의
// FNV-1a 해시를 **런타임 타입 정체성**으로 쓴다(ReflectionYml 의 ComponentTypeID
// 상수가 fnv1a_64("Component") 로 같은 값을 재현한다). 모듈에 붙은 타입의
// 서명 문자열에 모듈 이름 같은 장식이 붙으면, 타입을 모듈로 옮기는 순간 그
// 타입의 ID 가 조용히 바뀐다.
//
// 1단계에서는 그런 타입이 없으므로 관찰만 한다. 실제 문자열을 INFO 줄로 남겨
// 2단계 설계가 그 값을 보고 정하게 한다.
#include <cstdio>
#include <string_view>
#include <typeinfo>

#include <windows.h>
#include "TypeTrait.h"

import ce.probe.typename_owner;

int main()
{
	constexpr std::string_view name = TypeTrait::type_name<CppModuleOwnedComponent>();
	std::printf("INFO: type_name=%.*s\n", static_cast<int>(name.size()), name.data());
	std::printf("INFO: typeid.name=%s\n", typeid(CppModuleOwnedComponent).name());

	const bool same = (name == "CppModuleOwnedComponent");
	std::printf("%s\n", same ? "CPP_MODULE_TYPENAME_OK=true" : "CPP_MODULE_TYPENAME_DECORATED");
	return same ? 0 : 1;
}
