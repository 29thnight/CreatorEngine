#pragma once
#include <cstdint>
#include <string>
#include <vector>

// SerializationPlan D3-b-2b-1b-3a — **어댑터 수준** 파리티.
//
// 앞선 두 프로브가 증명하지 못하는 축이다:
//   · D3-b-0(파서 파리티)은 두 파서가 만든 **트리**가 같은지 쟀다.
//   · D3-b-2b-0(스칼라 파리티)은 **값 변환**이 같은지 쟀다.
//   · 그러나 소비자가 실제로 부르는 것은 `ReadNode`의 연산이다 —
//     `IsNull`·`IsScalar`·`IsMap`·`Size`·`operator[]`·맵 순회·`Scalar()`.
//     그 아홉 가지가 두 backend에서 같은 답을 내는지는 **아직 아무도 재지 않았다.**
//
// ★ 특히 위험한 것이 backend 비대칭이다. yaml-cpp에서 맵의 키는 **진짜 노드**지만
//   ryml에서는 **자식 노드의 속성**이다. 널 판정도 yaml-cpp는 노드 타입이고 ryml은
//   "값이 있는데 그 값이 null 표기"다. 어댑터가 그 비대칭을 흡수하는데, 흡수가
//   맞는지는 같은 문서를 양쪽에 넣어 봐야만 알 수 있다.
//
// ★ 이 프로브는 **읽기만** 한다. 저작 자산을 쓰지 않으므로 코퍼스를 오염시키지 않는다.
namespace Authoring
{
	struct AdapterParityResult
	{
		std::uint32_t files{ 0 };

		// 두 backend에서 모두 방문한 노드 수. 0이면 "차이 0"이 아무것도 뜻하지 않는다.
		std::uint64_t comparedNodes{ 0 };

		// 연산별 불일치 수. 어느 연산이 갈렸는지가 진단의 전부다.
		std::uint32_t divergences{ 0 };
		std::string firstDivergence{};

		// `.asset`은 확장자 하나가 두 포맷을 담는다(재질=YAML, 모델=CEMA 바이너리).
		// 건너뛴 것은 "문제없음"이 아니라 "확인하지 않음"이다.
		std::uint32_t skippedBinary{ 0 };

		// 한쪽만 파싱에 실패한 경우. 파서 파리티(D3-b-0)가 이미 0을 단정하므로
		// 여기서 0이 아니면 그 전제가 깨진 것이다.
		std::uint32_t parseFailures{ 0 };

		// 실제로 맵 순회를 몇 번 했는가. 0이면 키 비대칭을 검사하지 않은 것이다.
		std::uint64_t comparedMapEntries{ 0 };
	};

	// 주어진 파일들을 두 backend로 파싱해 어댑터 연산을 전수 대조한다.
	[[nodiscard]] AdapterParityResult ProbeAdapterParity(const std::vector<std::string>& utf8Paths);
}
