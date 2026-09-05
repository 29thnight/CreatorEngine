#pragma once
#include "Core.Minimal.h"
#include <cassert>

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
		assert(!str.empty() && "HashingString: 빈 문자열로 생성했다");
		m_string = str;
		m_hash = std::hash<std::string_view>{}(str);
	}

	HashingString(std::string_view str)
	{
		assert(!str.empty() && "HashingString: 빈 문자열로 생성했다");
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
		assert(!str.empty() && "HashingString: 빈 문자열을 대입했다");
		m_string = str;
		m_hash = std::hash<std::string_view>{}(m_string);
		return *this;
	}

	HashingString& operator=(std::string_view str)
	{
		assert(!str.empty() && "HashingString: 빈 문자열을 대입했다");
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

	// 해시가 같아도 문자열까지 봐야 <=>와 같은 판정이 된다(PHASE 15 H-c).
	// 예전에는 해시만 비교해서, 충돌이 나면 ==는 "같다" <=>는 "다르다"를
	// 냈다 — 둘을 함께 쓰는 표준 컨테이너·알고리즘에서 조용한 오작동이다.
	// 해시가 먼저 걸러 주므로 비용은 사실상 그대로다.
	bool operator==(const HashingString& other) const
	{
		return m_hash == other.m_hash && m_string == other.m_string;
	}

	// operator!=는 컴파일러가 ==에서 만들어 준다. 손으로 쓰던 판은 해시만
	// 비교해서 ==와 어긋날 수 있었다.

	// 값 반환이었다(H-d). 이름 하나를 비교하려고 문자열을 통째로 복사하는
	// 것이 이 클래스를 쓰는 목적과 정반대라, 참조로 바꿨다.
	const std::string& ToString() const noexcept
	{
		return m_string;
	}

	void SetString(std::string_view str)
	{
		if (nullptr == str.data())
		{
			throw std::invalid_argument("Null pointer provided in SetString.");
		}
		m_string = str;
		m_hash = std::hash<std::string_view>{}(str);
	}

	// 내부 버퍼의 쓰기 권한을 밖으로 주지 않는다(H-a의 근본 원인·H-g).
	// char*를 열어 두면 밖에서 문자열을 고쳐도 m_hash가 그대로라 캐시가
	// 어긋나고, 그 뒤의 == 결과는 전부 신뢰할 수 없게 된다. const가 없어서
	// const&로는 size()조차 부를 수 없던 것도 같은 줄의 문제였다.
	const char* data() const noexcept { return m_string.c_str(); }
	size_t size() const noexcept { return m_string.size(); }

	// 해시 컨테이너 키로 쓰기 위한 접근자(H-e). 이미 계산해 둔 값을 그대로 낸다.
	size_t GetHash() const noexcept { return m_hash; }

private:
	size_t m_hash{};
	std::string m_string{};
};

// unordered 컨테이너의 키 계약. ==가 해시+문자열을 함께 보므로 이 특수화와
// 짝이 맞는다.
namespace std
{
	template<>
	struct hash<HashingString>
	{
		size_t operator()(const HashingString& value) const noexcept
		{
			return value.GetHash();
		}
	};
}
