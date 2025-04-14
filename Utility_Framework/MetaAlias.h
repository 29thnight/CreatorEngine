#pragma once
#include <functional>
#include <span>
#include "TypeTrait.h"

namespace Meta
{
	struct MethodParameter;
	struct Property;
	struct Method;

	using GetterType = std::function<std::any(void* instance)>;
	using SetterType = std::function<void(void* instance, std::any value)>;
	using Invoker = std::function<std::any(void* instance, const std::vector<std::any>& args)>;

	using TypeInfo = std::type_info;
	using OffsetType = std::ptrdiff_t;
	using MethodParameterContainer = std::vector<MethodParameter>;
	template<typename T>
	using View = std::span<T>;
}