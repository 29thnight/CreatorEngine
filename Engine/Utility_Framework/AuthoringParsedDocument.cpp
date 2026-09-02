#include "AuthoringParsedDocument.h"

#include "AuthoringCookedDocument.h"
#include "AuthoringParseTelemetry.h"
#include "AuthoringRymlErrorPolicy.h"

#include <atomic>
#include <fstream>
#include <mutex>
#include <sstream>

namespace Authoring
{
	namespace
	{
		std::atomic<std::uint64_t> g_textParseCalls{};
		std::mutex g_textParseContextMutex;
		std::vector<std::string> g_textParseContexts;
		constexpr std::size_t kMaxTextParseContexts = 64u;
	}

	void RecordTextParserCall(std::string_view context) noexcept
	{
		g_textParseCalls.fetch_add(1u, std::memory_order_relaxed);
		try
		{
			std::lock_guard lock(g_textParseContextMutex);
			if (g_textParseContexts.size() < kMaxTextParseContexts)
				g_textParseContexts.emplace_back(context);
		}
		catch (...)
		{
			// Telemetry must never change parser behavior.
		}
	}

	TextParseTelemetrySnapshot GetTextParseTelemetry() noexcept
	{
		TextParseTelemetrySnapshot snapshot;
		snapshot.calls = g_textParseCalls.load(std::memory_order_relaxed);
		try
		{
			std::lock_guard lock(g_textParseContextMutex);
			snapshot.contexts = g_textParseContexts;
		}
		catch (...)
		{
		}
		return snapshot;
	}

	ParsedDocument ParsedDocument::ParseText(const std::string& text, std::string& error)
	{
		return ParseTextWithContext(text, error, "<memory>");
	}

	ParsedDocument ParsedDocument::ParseTextWithContext(const std::string& text,
		std::string& error, std::string_view context)
	{
		// ★ 정책이 먼저다. 없으면 잘못된 문서 하나가 파싱 실패가 아니라
		//   **프로세스 abort**가 된다(D3-b-1).
		EnsureRymlErrorPolicy();

		ParsedDocument document;
		error.clear();

		// ★ 모델 캐시 `CEMA`는 YAML이 아니다. `.asset` 확장자가 두 포맷을 담으므로
		//   확장자만으로는 가를 수 없고, 넣으면 ryml이 죽는다.
		if (text.size() >= 4 && 0 == text.compare(0, 4, "CEMA"))
		{
			error = "바이너리 자산이다(CEMA) — YAML이 아니다";
			return document;
		}

		try
		{
			RecordTextParserCall(context);
			// `parse_in_arena`는 입력을 트리의 arena로 복사한다. 그래서 이 함수가
			// 끝난 뒤에도 스칼라 뷰가 유효하다 — 원문을 따로 붙잡을 필요가 없다.
			document.m_tree = ryml::parse_in_arena(ryml::to_csubstr(text));
			document.m_valid = (document.m_tree.size() > 0);
			if (!document.m_valid) error = "빈 문서";
		}
		catch (const std::exception& exception)
		{
			document.m_valid = false;
			error = exception.what();
		}
		return document;
	}

	ParsedDocument ParsedDocument::ParseFile(const std::string& utf8Path, std::string& error)
	{
		std::ifstream input(utf8Path, std::ios::binary);
		if (!input)
		{
			error = "파일을 열 수 없다: " + utf8Path;
			return ParsedDocument{};
		}
		std::ostringstream buffer;
		buffer << input.rdbuf();
		const std::string bytes = buffer.str();
		const std::span<const std::byte> byteView{
			reinterpret_cast<const std::byte*>(bytes.data()), bytes.size() };
		if (IsCookedDocument(byteView)) return ParseCooked(byteView, error);
		return ParseTextWithContext(bytes, error, utf8Path);
	}

	ParsedDocument ParsedDocument::ParseCooked(
		std::span<const std::byte> bytes, std::string& error)
	{
		ParsedDocument document;
		auto decoded = DecodeCookedDocument(bytes, error);
		if (!decoded) return document;
		document.m_tree = std::move(decoded->m_tree);
		document.m_valid = document.m_tree.size() > 0u;
		if (!document.m_valid) error = "빈 cooked document";
		return document;
	}

	ReadNode ParsedDocument::Root() const
	{
		if (!m_valid) return ReadNode{};
		return ReadNode::FromRyml(m_tree);
	}
}
