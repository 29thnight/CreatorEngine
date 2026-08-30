#pragma once
#include <cstdint>
#include <string>

// SerializationPlan D3-b-1 — ryml을 제품 경로에 넣기 위한 **선결 조건**.
//
// ★ ryml의 기본 에러 처리는 예외도 반환값도 아니라 **프로세스 abort**다.
//   저작 문서 하나가 ryml이 거부하는 구문을 담고 있으면 에디터가 그 자리에서
//   죽는다 — 가설이 아니라 실측이다. D3-b-0 프로브를 처음 돌렸을 때 두 번
//   abort했다:
//     · CRLF (`parser_state.hpp: check failed: rem.find('\r') == npos`)
//     · `multiline scalars cannot be used as keys`
//   yaml-cpp는 두 경우 모두 예외를 던졌다. 즉 backend를 바꾸는 것만으로
//   **"로드 실패"가 "프로세스 사망"으로 승격된다.** 그래서 파서를 옮기기 전에
//   이 정책이 먼저 있어야 한다.
//
// ★ **범위 RAII가 아니라 전역 1회 설치인 이유.** `ryml::set_callbacks`는 프로세스
//   전역을 쓴다. 파싱마다 설치·복원하면 씬 로드의 배치 경로처럼 여러 스레드가
//   동시에 파싱할 때 한 스레드의 복원이 다른 스레드의 설치를 지운다 — 그 순간
//   abort 방어가 사라지고, 하필 그때만 죽으므로 재현되지 않는다. 설치는 한
//   번이고 복원은 없다.
//
// ★ 이 헤더에 ryml 타입이 없다. `AuthoringDocument.h`와 같은 규율이다 —
//   포맷을 아는 것은 이 파일의 `.cpp`뿐이고, 호출자는 정책을 켜기만 한다.
namespace Authoring
{
	// ryml 에러를 abort 대신 `std::runtime_error`로 만든다. 여러 번 불러도
	// 설치는 한 번이며 스레드 안전하다. ryml로 파싱하는 모든 경로가 파싱 **전에**
	// 이것을 부른다.
	void EnsureRymlErrorPolicy();

	// ── 정책이 실제로 살아 있는지 재는 프로브 ────────────────────────────────
	//
	// 이 정책은 "설치했다"를 정적으로 확인해 봐야 아무것도 증명하지 못한다.
	// ryml이 셋으로 나눈 에러 채널(basic/parse/visit) 중 하나만 덮으면 나머지는
	// 여전히 abort하고, 그 사실은 **실제로 그 채널을 건드려야** 드러난다.
	// 그래서 게이트는 ryml을 일부러 실패시키고 프로세스가 살아남는지 본다 —
	// 정책이 없으면 게이트는 실패가 아니라 **크래시**한다. 그것이 이 검사의 이빨이다.
	struct RymlErrorPolicyProbe
	{
		// ── 실측으로 고른 입력들 ────────────────────────────────────────────
		//
		// ★ 처음에는 "CRLF"와 "멀티라인 스칼라 키"를 재현으로 썼는데 **둘 다
		//   ryml이 조용히 받아들였다.** 14종을 실제로 태워 보고서야 어떤 입력이
		//   어느 채널을 여는지 알았다. 재현은 지어내는 것이 아니라 재는 것이다.
		//
		//   특히 **CRLF는 ryml 0.16이 정상 파싱한다**(단순 맵·시퀀스·블록 스칼라·
		//   주석 모두 통과). D3-b-0 프로브가 정규화를 넣은 근거였던 "CRLF는 abort"는
		//   과잉 일반화였다 — 실제 트리거는 **홀로 선 CR**이다.

		// 홀로 선 CR(옛 Mac 개행) — `check failed: rem.find('') == npos`.
		bool threwOnLoneCr{ false };

		// 탭 들여쓰기 — YAML이 금지하는 형태. parse 채널.
		bool threwOnTabIndent{ false };

		// 정상 문서는 여전히 파싱돼야 한다. 정책이 모든 파싱을 던지게 만들어
		// 놓고 위 둘이 참인 것을 통과로 읽지 않기 위한 대조군이다.
		bool parsedValidDocument{ false };

		// CRLF 대조군 — 이것이 거짓이 되면 ryml이 개행 처리를 바꿨다는 뜻이고,
		// 그때는 정규화 사본이 다시 필요해진다(성능 계산이 달라진다).
		bool parsedCrLfDocument{ false };

		// 두 실패가 **서로 다른 채널**을 탔는가. 한 채널만 설치해도 두 입력이
		// 모두 던질 수 있으므로, 채널 태그가 갈리는지 봐야 "셋 다 덮었다"에
		// 가까워진다.
		bool coveredDistinctChannels{ false };

		// 처음 잡힌 메시지(채널 태그 포함). 비어 있으면 "예외가 던져졌다"가 공허하다.
		std::string firstMessage{};
	};

	// ★ `m_error_visit`은 여기서 재지 않는다. 그 채널은 파싱이 아니라 트리 순회
	//   (없는 키 역참조 등)에서 열리므로 파서 입력만으로는 닿지 않는다. 세 채널을
	//   모두 설치하되 검사는 둘만 덮는다는 사실을 숨기지 않는다 — D3-b-2가 ryml
	//   노드를 실제로 순회할 때 그 채널을 덮는 검사를 함께 만든다.
	[[nodiscard]] RymlErrorPolicyProbe ProbeRymlErrorPolicy();

	// ── 정책 아래에서의 파싱 시도 ─────────────────────────────────────────────
	//
	// 게이트가 "무엇이 실제로 abort를 일으키는가"를 **실측**할 수 있어야 한다.
	// 합성 입력을 머리로 지어내면 틀린다 — 실제로 처음 만든 재현 둘을 ryml이
	// 조용히 받아들였다. 그래서 입력을 밖에서 주고 결과를 돌려받는 창구를 둔다.
	// 이것은 D3-b-2가 쓸 첫 제품 경로 파싱 진입점이기도 하다.
	struct RymlParseAttempt
	{
		bool parsed{ false };
		bool threw{ false };
		std::uint64_t nodeCount{ 0 };
		std::string message{};
	};

	// 정책을 보장한 뒤 `text`를 있는 그대로(정규화 없이) 파싱한다. abort하면
	// 프로세스가 죽는다 — 그것이 이 함수로 재려는 사실이다.
	[[nodiscard]] RymlParseAttempt TryParseWithPolicy(const std::string& text);
}
