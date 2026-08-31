#pragma once
#include <cstdint>
#include <string>
#include <limits>
#include <string_view>
#include <type_traits>

// SerializationPlan D3-b-2b-1a — 스칼라 **변환**을 backend에서 떼어낸다.
//
// ★ 왜 이것이 파서 교체보다 먼저인가. D3-b-2b-0이 44케이스를 재서 **11건이
//   갈린다**는 것을 확인했다 — yaml-cpp는 `.inf`·`.nan`과 YAML 1.1 불리언
//   (`yes`/`no`/`on`/`off`)을 읽지만 ryml은 거부하고, `1`/`0`은 **방향이 뒤집혀**
//   yaml-cpp가 거부하고 ryml이 읽는다. 파서만 바꾸면 트리 구조는 그대로인 채
//   **값의 의미가 조용히 달라진다.** 로드는 성공하고 값만 틀린다.
//
//   그래서 변환을 **문자열 위의 함수**로 먼저 내린다. 노드 타입을 모르므로
//   backend 교체가 이 계층을 건드리지 않고, D3-b-2b-1b는 트리 순회만 바꾸면 된다.
//
// ★ **재구현이 아니라 이식이다.** 의미를 머리로 다시 짜면 미묘하게 어긋난다
//   (실제로 D3-b-1에서 재현 입력 둘을 지어냈다가 둘 다 틀렸다). 여기서는
//   yaml-cpp `convert<T>`가 쓰는 것과 같은 알고리즘을 문자열 위에 놓고,
//   **게이트가 두 결과를 전수 대조해 차이 0을 단정**한다.
//
// ★ 실패는 예외가 아니라 `false`다. 호출부가 "부재"와 "변환 실패"를 가려야 하고,
//   yaml-cpp의 `BadConversion` 예외 의미는 호출부에서 되살린다 —
//   이 계층이 예외를 던지면 ryml 경로에서 두 가지 실패 표현이 섞인다.
namespace Authoring::Scalar
{
	// YAML 1.1 불리언. yaml-cpp가 받는 표기의 **전부**이며, `1`/`0`은 포함되지
	// 않는다(실측: yaml-cpp `as<bool>`이 "1"에서 실패한다).
	[[nodiscard]] bool TryParseBool(std::string_view text, bool& out);

	// 정수. yaml-cpp가 그러듯 진법 접두사(`0x`·선행 `0`)를 인식한다.
	[[nodiscard]] bool TryParseInt64(std::string_view text, std::int64_t& out);
	[[nodiscard]] bool TryParseUInt64(std::string_view text, std::uint64_t& out);

	// 부동소수. `.inf`·`-.inf`·`.nan`을 대소문자 무시로 받는다.
	[[nodiscard]] bool TryParseFloat(std::string_view text, float& out);
	[[nodiscard]] bool TryParseDouble(std::string_view text, double& out);

	// ── 타입 디스패치 ────────────────────────────────────────────────────────
	//
	// 위 네 함수를 임의의 산술 타입에 맞춰 고른다. 좁은 타입으로의 절단을
	// 성공으로 읽지 않는 것이 핵심이다 — yaml-cpp도 범위를 벗어난 값에서 실패한다.
	template<class T>
	[[nodiscard]] inline bool TryConvert(std::string_view raw, T& out)
	{
		if constexpr (std::is_same_v<T, bool>)
		{
			return TryParseBool(raw, out);
		}
		else if constexpr (std::is_floating_point_v<T>)
		{
			if constexpr (std::is_same_v<T, float>)
			{
				return TryParseFloat(raw, out);
			}
			else
			{
				double value{};
				if (!TryParseDouble(raw, value)) return false;
				out = static_cast<T>(value);
				return true;
			}
		}
		else if constexpr (std::is_signed_v<T>)
		{
			std::int64_t value{};
			if (!TryParseInt64(raw, value)) return false;
			if (value < static_cast<std::int64_t>((std::numeric_limits<T>::min)())
				|| value > static_cast<std::int64_t>((std::numeric_limits<T>::max)()))
			{
				return false;
			}
			out = static_cast<T>(value);
			return true;
		}
		else
		{
			std::uint64_t value{};
			if (!TryParseUInt64(raw, value)) return false;
			if (value > static_cast<std::uint64_t>((std::numeric_limits<T>::max)()))
			{
				return false;
			}
			out = static_cast<T>(value);
			return true;
		}
	}
}

