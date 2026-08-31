#pragma once
#include <cstdint>
#include <string>
#include <vector>

// SerializationPlan D3-b-2 — 스칼라 **변환** 파리티. 구조 파리티와 다른 문제다.
//
// D3-b-0의 파서 프로브는 두 파서가 만든 트리가 **구조적으로 같은지**를 쟀다 —
// 스칼라는 문자열로만 비교했다. 그것으로 증명되지 않는 것이 이것이다:
//
//     yaml-cpp `node.as<bool>()` == ryml `from_chars(val, &b)` 인가?
//
// 같은 문자열 `"yes"`를 yaml-cpp는 YAML 1.1 규약으로 `true`로 읽고 ryml은 다르게
// 읽을 수 있다. 그러면 **파서를 바꾼 순간 저장된 값의 의미가 조용히 달라진다** —
// 트리 구조는 여전히 같으므로 D3-b-0 게이트는 통과한다. 로드는 성공하고 값만
// 틀린다. 검사 없이 넘어가면 가장 늦게, 가장 비싸게 드러나는 종류의 결함이다.
//
// 변환 경계는 좁다: `ReflectionTypedYml.h`의 `ReadScalar` 오버로드 15종이고,
// 그것들이 실제로 부르는 것은 `as<std::string>`·`as<float>`·`as<bool>`·
// `as<size_t>`·`as<int>`·`as<std::uint32_t>` 여섯이다. 이 프로브는 그 여섯을 잰다.
//
// ★ **이 프로브는 "같아야 한다"고 단정하지 않는다.** 갈리는 지점을 **찾는** 것이
//   목적이고, 갈리면 D3-b-2가 그 자리에 명시적 변환을 넣어야 한다는 뜻이다.
//   게이트는 "차이 0"이 아니라 "**알려진 차이 목록과 일치**"를 단정한다 — 그래야
//   새로 생긴 차이만 빨개진다.
namespace Authoring
{
	struct ScalarParityCase
	{
		std::string name{};

		// 읽을 타입. 문자열로 두는 이유는 게이트가 사람이 읽는 표를 찍기 때문이다.
		std::string type{};

		// 입력 YAML 문서 조각. 항상 `v: <스칼라>` 꼴이며 키 `v`를 읽는다.
		std::string document{};

		bool yamlCppOk{ false };
		bool rymlOk{ false };

		// 읽어 낸 값을 **문자열로 정규화**한 것. 타입별 비교를 한 축으로 모은다.
		std::string yamlCppValue{};
		std::string rymlValue{};

		// 둘 다 성공했고 값이 같은가. 실패 여부가 갈리는 것도 불일치다.
		bool agrees{ false };

		// ── D3-b-2b-1a: 이식한 변환기(`Authoring::Scalar`) ───────────────────
		//
		// ryml과의 차이는 **허용된 목록**이지만, 이식 변환기와 yaml-cpp의 차이는
		// **0이어야 한다.** 이 계층의 목적이 곧 "backend가 바뀌어도 값의 의미가
		// 그대로"이기 때문이다. 차이가 하나라도 있으면 이식이 실패한 것이다.
		bool converterOk{ false };
		std::string converterValue{};
		bool converterAgrees{ false };
	};

	struct ScalarParityResult
	{
		std::vector<ScalarParityCase> cases{};
		std::uint32_t agreeCount{ 0 };
		std::uint32_t divergeCount{ 0 };

		// 이식 변환기 대 yaml-cpp. 0이어야 한다.
		std::uint32_t converterDivergeCount{ 0 };
	};

	[[nodiscard]] ScalarParityResult ProbeScalarConversions();
}
