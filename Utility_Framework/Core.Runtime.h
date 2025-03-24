#pragma once
#include "ClassProperty.h"

class ThrowHelperInternal final : public Noncopyable
{
public:
	static inline void ThrowLogicErrorException(const std::string_view& message)
	{
		throw std::logic_error(message.data());
	}

	static inline void ThrowDomainErrorException(const std::string_view& message)
	{
		throw std::domain_error(message.data());
	}

	static inline void ThrowInvalidArgException(const std::string_view& message)
	{
		throw std::invalid_argument(message.data());
	}

	static inline void ThrowLengthErrorException(const std::string_view& message)
	{
		throw std::length_error(message.data());
	}

	static inline void ThrowOutOfRangeException(const std::string_view& message)
	{
		throw std::out_of_range(message.data());
	}

	static inline void ThrowRuntimeException(const std::string_view& message)
	{
		throw std::runtime_error(message.data());
	}

	static inline void ThrowOverflowException(const std::string_view& message)
	{
		throw std::overflow_error(message.data());
	}

	static inline void ThrowUnderflowException(const std::string_view& message)
	{
		throw std::underflow_error(message.data());
	}

	static inline void ThrowRangeErrorException(const std::string_view& message)
	{
		throw std::range_error(message.data());
	}
};