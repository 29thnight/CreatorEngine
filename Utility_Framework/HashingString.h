#pragma once
#include "Core.Minimal.h"

class HashingString
{
public:
	HashingString() = default;
	HashingString(const char* str)
	{
		m_string = str;
		m_hash = std::hash<std::string_view>{}(str);
	}
	HashingString(const std::string& str)
	{
		m_string = str;
		m_hash = std::hash<std::string_view>{}(str);
	}
	HashingString(const HashingString& other)
	{
		m_string = other.m_string;
		m_hash = other.m_hash;
	}
	HashingString(HashingString&& other) noexcept
	{
		m_string = other.m_string;
		m_hash = other.m_hash;
	}
	HashingString& operator=(const HashingString& other)
	{
		m_string = other.m_string;
		m_hash = other.m_hash;
		return *this;
	}
	HashingString& operator=(HashingString&& other) noexcept
	{
		m_string = other.m_string;
		m_hash = other.m_hash;
		return *this;
	}
	bool operator==(const HashingString& other) const
	{
		return m_hash == other.m_hash;
	}
	bool operator!=(const HashingString& other) const
	{
		return m_hash != other.m_hash;
	}
	bool operator<(const HashingString& other) const
	{
		return m_hash < other.m_hash;
	}
	bool operator>(const HashingString& other) const
	{
		return m_hash > other.m_hash;
	}
	bool operator<=(const HashingString& other) const
	{
		return m_hash <= other.m_hash;
	}
	bool operator>=(const HashingString& other) const
	{
		return m_hash >= other.m_hash;
	}
	std::string ToString() const
	{
		return m_string;
	}

private:
	size_t m_hash{};
	std::string m_string{};
};