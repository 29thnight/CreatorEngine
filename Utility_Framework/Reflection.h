#pragma once
#include <rfl.hpp>
#include <rfl/json.hpp>
#include "TypeDefinition.h"

#define GENERATE_BODY(Type) \
using ReflectionType = Type; \
friend rfl::Reflector<Type>; \
static uint32_t hash() noexcept \
{ \
	return std::type_index(typeid(ReflectionType)).hash_code(); \
} \

#define REFLECTION_PROPERTY(Type, Name) \
Type Name; \

#define REFLECTION_TO_METHOD(TYPE,...) \
TYPE rfl::Reflector<TYPE>::to(const ReflType& value) noexcept \
{ \
	return { __VA_ARGS__ }; \
} \
\
rfl::Reflector<TYPE>::ReflType rfl::Reflector<TYPE>::from(const TYPE& value) \
{ \
	return { __VA_ARGS__ }; \
} \

#define GENERATE_REFLECTION(TYPE, ...) \
template <> \
struct rfl::Reflector<TYPE> \
{ \
	struct ReflType \
	{ \
		__VA_ARGS__ \
	}; \
	\
	static TYPE to(const ReflType& v) noexcept; \
	static ReflType from(const TYPE& v); \
}; \
