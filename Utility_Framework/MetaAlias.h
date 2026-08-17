#pragma once
#include <functional>
#include <span>
#include <any>
#include "TypeTrait.h"

namespace Meta
{
	struct MethodParameter;
	struct Property;
	struct Method;

	// GetterType(any 게터)은 CT10 감사에서 삭제 — Property::getter의 유일
	// 호출자(MakePropChangeCommand)가 자체 사망 상태였다.
	using SetterType = std::function<void(void* instance, std::any value)>;
	using Invoker = std::function<std::any(void* instance, const std::vector<std::any>& args)>;

	using TypeInfo = std::type_info;
	using OffsetType = std::ptrdiff_t;
	using MethodParameterContainer = std::vector<MethodParameter>;
	template<typename T>
	using View = std::span<T>;
}
