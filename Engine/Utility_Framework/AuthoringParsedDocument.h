#pragma once
#include "AuthoringReadNode.h"

#include <cstddef>
#include <span>
#include <string>
#include <string_view>

// SerializationPlan D3-b-L — **파싱 결과를 소유하는** 홀더.
//
// ★ 왜 별도 타입인가. ryml의 노드 뷰는 트리를 소유하지 않는다 — `Tree`가 죽으면
//   그 트리에서 나온 모든 `ReadNode`가 dangling이 된다(§8 "ryml view 수명 오용").
//   yaml-cpp `Node`는 값 의미론이라 이 규칙이 없었고, **그 비대칭이 전환에서 가장
//   위험한 지점**이다. 홀더가 트리를 붙잡아 그 규칙을 타입으로 강제한다.
//
// ★ `Authoring::Document`와 다른 것이다. 그쪽은 **저작 문서의 장기 소유**(프리팹
//   정의·씬 백업)이고 이쪽은 **한 번 파싱한 결과의 스코프 소유**다. 이름이 비슷해
//   헷갈리기 쉬우므로 용도를 여기 적어 둔다.
//
// ★ 이동만 가능하다. 복사가 조용히 일어나면 트리가 복제되어 비싸고, 어느 트리를
//   가리키는 뷰인지가 흐려진다.
//
// ── leaf 파서용 ──────────────────────────────────────────────────────────────
//
// 이 저장소에는 자기 파일만 읽고 평범한 데이터를 내놓는 파서가 여럿 있다
// (`ShaderMeta`·`TagManager`·`BlackBoard`·`EditorSettingsStore` 등, 실측 약 68곳).
// 이들은 소비자가 backend에 묶여 있지 않아 **씬 경로보다 먼저 ryml로 옮길 수 있다** —
// 제품 경로의 첫 ryml이고, 전환을 끝에서 검증하는 대신 여기서 먼저 밟아 본다.
namespace Authoring
{
	class ParsedDocument
	{
	public:
		ParsedDocument() = default;

		ParsedDocument(ParsedDocument&&) noexcept = default;
		ParsedDocument& operator=(ParsedDocument&&) noexcept = default;
		ParsedDocument(const ParsedDocument&) = delete;
		ParsedDocument& operator=(const ParsedDocument&) = delete;

		// 파일을 읽어 파싱한다. 실패하면 유효하지 않은 문서를 돌려주고 `error`에
		// 이유를 담는다 — **예외를 던지지 않는다.** 호출부가 "파일 없음"과 "파싱
		// 실패"를 각자의 방식으로 다뤄 왔으므로 그 선택을 빼앗지 않는다.
		[[nodiscard]] static ParsedDocument ParseFile(const std::string& utf8Path,
			std::string& error);

		// 이미 읽어 둔 텍스트를 파싱한다(임베디드 payload 등).
		[[nodiscard]] static ParsedDocument ParseText(const std::string& text,
			std::string& error);

		// CEDO만 받는 strict runtime 경계. 구버전 YAML payload를 여기서
		// 호환하지 않는다; producer 변경 뒤의 artifact는 재쿠킹해야 한다.
		[[nodiscard]] static ParsedDocument ParseCooked(
			std::span<const std::byte> bytes, std::string& error);

		[[nodiscard]] bool IsValid() const noexcept { return m_valid; }
		explicit operator bool() const noexcept { return m_valid; }

		// ★ 돌려준 노드는 **이 문서보다 오래 살 수 없다.** 트리가 여기 있다.
		[[nodiscard]] ReadNode Root() const;

	private:
		[[nodiscard]] static ParsedDocument ParseTextWithContext(
			const std::string& text, std::string& error, std::string_view context);

		bool m_valid{ false };
		ryml::Tree m_tree{};
	};
}
