#pragma once
#include <cstdint>
#include <string>

// SerializationPlan D3-b-0 — ryml이 이 저장소의 저작 문서를 **정확히** 읽는지 재는 프로브.
//
// 왜 전환보다 이것이 먼저인가: D3-b는 yaml-cpp 소비 53파일·408매치를 바꾼다. 그 규모를
// 옮기고 나서 무언가 어긋나면 "ryml이 다르게 읽은 것"과 "옮기다 틀린 것"을 가를 수 없다.
// 그래서 **같은 문서를 두 파서로 읽어 구조가 같은지** 먼저 증명하고, 그 다음에 옮긴다.
//
// 함께 재는 것이 파싱 시간이다. D0 기준선은 씬 로드의 60%가 텍스트 파싱이라고 말했고,
// D3-b의 이득 상한은 그 몫이다 — 이 프로브의 배율이 그 상한을 실측값으로 바꾼다.
//
// ★ 이 프로브는 **읽기만** 한다. 저작 자산을 쓰지 않으므로 코퍼스를 오염시키지 않는다.
namespace Authoring
{
	struct ParserProbeResult
	{
		bool parsedByYamlCpp{ false };
		bool parsedByRyml{ false };

		// 두 파서가 만든 트리가 구조적으로 같은가(스칼라 문자열까지).
		bool structurallyEqual{ false };

		// 처음 갈라진 지점. 같으면 비어 있다.
		std::string firstDifference{};

		// ryml이 거부한 이유(파싱 실패 시). yaml-cpp가 받아들이는 문서를 ryml이
		// 거부할 수 있고, 그 목록이 D3-b 판단의 근거다.
		std::string rymlError{};

		// ryml은 CRLF를 만나면 abort한다. 이 저장소의 저작 자산은 CRLF이므로
		// 정규화가 전제이며, 그 사실을 결과가 드러내야 한다.
		// 이 파일은 YAML이 아니라 바이너리 자산이다(모델 캐시 `CEMA`). `.asset`
		// 확장자가 두 포맷을 담으므로 확장자만으로는 가를 수 없다. 건너뛴 것을
		// "문제없음"이 아니라 "확인하지 않음"으로 세기 위한 표시다.
		bool skippedBinaryAsset{ false };

		bool normalizedCrLf{ false };

		std::uint64_t yamlCppNanoseconds{ 0 };
		std::uint64_t rymlNanoseconds{ 0 };

		// 비교한 노드 수. 0이면 "같다"가 아무것도 뜻하지 않는다 — 게이트가 이 값을
		// 함께 단정해야 빈 비교를 통과로 읽지 않는다.
		std::uint64_t comparedNodes{ 0 };
	};

	// 파일 하나를 두 파서로 읽고 대조한다. 파싱 실패도 결과에 담아 돌려준다(예외 없음).
	[[nodiscard]] ParserProbeResult ProbeParsers(const std::string& utf8Path);
}
