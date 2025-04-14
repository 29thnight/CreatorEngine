#pragma once
#include "Core.Minimal.h"

class HashingString
{
public:
	HashingString() = default;
	HashingString(const char* str)
	{
		if (str == nullptr) 
		{
			throw std::invalid_argument("Null pointer provided to HashingString constructor.");
		}
		m_string = str;
		m_hash = std::hash<std::string_view>{}(str);
	}

	HashingString(const std::string& str)
	{
		if (str.empty()) 
		{
            CORE_ASSERT_MSG(!str.empty(), "Empty string provided to HashingString constructor.");
		}
		m_string = str;
		m_hash = std::hash<std::string_view>{}(str);
	}

	HashingString& operator=(const char* str)
	{
		if (str == nullptr) 
		{
			throw std::invalid_argument("Null pointer provided in assignment operator for const char*.");
		}
		m_string = str;
		m_hash = std::hash<std::string_view>{}(m_string);
		return *this;
	}

	HashingString& operator=(const std::string& str)
	{
		if (str.empty()) 
		{
            CORE_ASSERT_MSG(!str.empty(), "Empty string provided in assignment operator for std::string.");
		}
		m_string = str;
		m_hash = std::hash<std::string_view>{}(m_string);
		return *this;
	}

	HashingString(const HashingString&) = default;
	HashingString(HashingString&&) noexcept = default;
	HashingString& operator=(const HashingString&) = default;
	HashingString& operator=(HashingString&&) noexcept = default;

	auto operator<=>(const HashingString& other) const 
	{
		if (auto cmp = m_hash <=> other.m_hash; cmp != 0)
			return cmp;
		return m_string <=> other.m_string;
	}

	bool operator==(const HashingString& other) const 
	{
		return m_hash == other.m_hash && m_string == other.m_string;
	}


	std::string ToString() const
	{
		return m_string;
	}

	void SetString(const std::string_view& str)
	{
		if (nullptr == str.data())
		{
			throw std::invalid_argument("Null pointer provided in SetString.");
		}
		m_string = str;
		m_hash = std::hash<std::string_view>{}(str);
	}

    constexpr char* data() { return m_string.data(); }
    constexpr size_t size() { return m_string.size(); }

private:
	size_t m_hash{};
	std::string m_string{};
};
