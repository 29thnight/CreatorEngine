#include "AuthoringScalarConvert.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <sstream>

namespace Authoring::Scalar
{
	namespace
	{
		// yaml-cpp의 불리언 표는 대소문자 변형을 **명시적으로** 나열한다.
		// 소문자로 접어서 비교하면 `yEs` 같은 표기까지 받아들여 원본보다 넓어진다.
		const char* const kTrue[] = {
			"y", "Y", "yes", "Yes", "YES",
			"true", "True", "TRUE",
			"on", "On", "ON"
		};
		const char* const kFalse[] = {
			"n", "N", "no", "No", "NO",
			"false", "False", "FALSE",
			"off", "Off", "OFF"
		};

		bool Matches(std::string_view text, const char* const* table, std::size_t count)
		{
			for (std::size_t i = 0; i < count; ++i)
			{
				if (text == table[i]) return true;
			}
			return false;
		}

		// yaml-cpp `convert<T>::decode`와 같은 절차다: 10진 강제를 풀어 진법
		// 접두사를 인식하게 하고, 공백을 건너뛰지 않으며, 남은 것이 공백뿐일 때만
		// 성공으로 본다. 부분 파싱("12abc")을 성공으로 읽지 않기 위한 조건이다.
		template<class T>
		bool StreamParse(std::string_view text, T& out)
		{
			std::istringstream stream{ std::string(text) };
			stream.unsetf(std::ios::dec);
			T value{};
			if (!(stream >> std::noskipws >> value)) return false;
			if (!(stream >> std::ws).eof()) return false;
			out = value;
			return true;
		}

		// `.inf`/`.nan` 계열. yaml-cpp는 선택적 부호 뒤에 `.inf`/`.nan`을
		// 대소문자 무시로 받는다.
		bool TryParseSpecialDouble(std::string_view text, double& out)
		{
			if (text.empty()) return false;
			std::size_t i = 0;
			double sign = 1.0;
			if (text[0] == '-') { sign = -1.0; i = 1; }
			else if (text[0] == '+') { i = 1; }
			if (i >= text.size() || text[i] != '.') return false;

			std::string rest;
			rest.reserve(text.size() - i - 1);
			for (std::size_t k = i + 1; k < text.size(); ++k)
			{
				rest.push_back(static_cast<char>(
					std::tolower(static_cast<unsigned char>(text[k]))));
			}
			if (rest == "inf")
			{
				out = sign * std::numeric_limits<double>::infinity();
				return true;
			}
			if (rest == "nan")
			{
				// NaN에는 부호를 싣지 않는다 — 비교가 어차피 성립하지 않고,
				// 부호 있는 NaN을 만들면 왕복 표기가 갈린다.
				out = std::numeric_limits<double>::quiet_NaN();
				return true;
			}
			return false;
		}
	}

	bool TryParseBool(std::string_view text, bool& out)
	{
		if (Matches(text, kTrue, sizeof(kTrue) / sizeof(kTrue[0])))
		{
			out = true;
			return true;
		}
		if (Matches(text, kFalse, sizeof(kFalse) / sizeof(kFalse[0])))
		{
			out = false;
			return true;
		}
		return false;
	}

	bool TryParseInt64(std::string_view text, std::int64_t& out)
	{
		return StreamParse(text, out);
	}

	bool TryParseUInt64(std::string_view text, std::uint64_t& out)
	{
		// ★ **음수를 먼저 거른다.** 처음엔 "yaml-cpp도 같은 스트림을 쓰니 동작이
		//   같을 것"이라 적었는데 **틀렸다** — 게이트가 잡았다. `operator>>`는
		//   부호 없는 타입에 "-1"을 넣으면 래핑된 최대값으로 **성공**시키지만,
		//   yaml-cpp `as<std::uint64_t>("-1")`은 실패한다. 그 차이를 두면
		//   저작 오타 하나가 조용히 18446744073709551615이 된다.
		std::size_t i = 0;
		while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) ++i;
		if (i < text.size() && text[i] == '-') return false;
		return StreamParse(text, out);
	}

	bool TryParseDouble(std::string_view text, double& out)
	{
		if (TryParseSpecialDouble(text, out)) return true;
		return StreamParse(text, out);
	}

	bool TryParseFloat(std::string_view text, float& out)
	{
		double value{};
		if (TryParseSpecialDouble(text, value))
		{
			out = static_cast<float>(value);
			return true;
		}
		return StreamParse(text, out);
	}
}
