#pragma once
#include "Core.Minimal.h"

// ── 빈 문자열 정책 (PHASE 15 H5, 2026-09-05 결정) ──
//
// **빈 문자열은 합법이다.** 예전에는 경로마다 넷으로 갈렸다 — 기본 생성은 통과,
// std::string·string_view 경로는 Debug 어설션으로 즉사, const char* 경로는
// nullptr만 막고 빈 문자열은 통과, SetString은 무검사. 어설션은 Release에서
// 사라지므로 그 "금지"는 규약이 아니라 Debug에서만 죽는 함정이었다.
//
// 합법으로 정한 이유는 강제 수단이 없어서가 아니라, 강제할 자리가 여기가 아니기
// 때문이다. 이름·태그는 저작 UI와 역직렬화가 매 입력마다 만드는 값이고, 빈 값은
// 잘못된 프로그램이 아니라 "사용자가 지웠다"는 정상 입력이다. "이름 없는 엔티티를
// 만들지 마라"는 상위 정책이며 실제로 그 층(멤버 기본값·인스펙터 커밋 가드)이
// 막고 있다. 이 값 타입은 빈 문자열을 다른 문자열과 똑같이 다룬다.
//
// nullptr은 여전히 던진다 — 그것은 정책이 아니라 UB 방지다.
class HashingString
{
public:
	// 기본 생성도 빈 문자열의 해시를 갖는다. `= default`였을 때는 m_hash가 0으로
	// 남아 `HashingString()`과 `HashingString("")`이 같은 내용인데 다르다고
	// 판정됐다(0 vs FNV offset basis) — 빈 상태가 둘이었다. 정책 이전의 결함이다.
	HashingString()
		: m_hash(std::hash<std::string_view>{}(std::string_view{}))
	{
	}

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
		m_string = str;
		m_hash = std::hash<std::string_view>{}(str);
	}

	HashingString(std::string_view str)
	{
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
		m_string = str;
		m_hash = std::hash<std::string_view>{}(m_string);
		return *this;
	}

	HashingString& operator=(std::string_view str)
	{
		// 길이가 있는데 포인터가 없는 뷰는 호출부 버그다 — 읽는 순간 UB다.
		// 빈 뷰(std::string_view{})는 읽을 것이 없으므로 정상 입력이다.
		if (nullptr == str.data() && 0 != str.size())
		{
			throw std::invalid_argument("Sized view with a null pointer provided to HashingString.");
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

	// 대입에 위임한다. 예전에는 여기만 검사 규칙이 또 달라서(널 포인터는 던지고
	// 빈 문자열은 통과) 정책이 넷으로 갈린 네 번째 갈래가 이 함수였다.
	void SetString(std::string_view str)
	{
		*this = str;
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
