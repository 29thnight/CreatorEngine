#include "../../Containers/ContainerAPI.h"
#include "AuthoringParsedDocument.h"

#include "AuthoringCookedDocument.h"
#include "AuthoringParseTelemetry.h"
#include "AuthoringRymlErrorPolicy.h"

#include "../Foundation/StandardLibrary.h"

namespace Authoring
{
	namespace
	{
		std::atomic<std::uint64_t> g_textParseCalls{};
		std::mutex g_textParseContextMutex;
		ce::dynamic_array<ce::utf8_string> g_textParseContexts;
		constexpr std::size_t kMaxTextParseContexts = 64u;
	}

	void RecordTextParserCall(ce::utf8_string_view context) noexcept
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

	ParsedDocument ParsedDocument::ParseText(ce::utf8_string_view text, ce::utf8_string& error)
	{
		return ParseTextWithContext(text, error, "<memory>");
	}

	ParsedDocument ParsedDocument::ParseTextWithContext(ce::utf8_string_view text,
		ce::utf8_string& error, ce::utf8_string_view context)
	{
		// ★ 정책이 먼저다. 없으면 잘못된 문서 하나가 파싱 실패가 아니라
		//   **프로세스 abort**가 된다(D3-b-1).
		EnsureRymlErrorPolicy();

		ParsedDocument document;

		// ★ 모델 캐시 `CEMA`는 YAML이 아니다. `.asset` 확장자가 두 포맷을 담으므로
		//   확장자만으로는 가를 수 없고, 넣으면 ryml이 죽는다.
		if (text.starts_with("CEMA"))
		{
			error.assign("바이너리 자산이다(CEMA) — YAML이 아니다");
			return document;
		}

		try
		{
			RecordTextParserCall(context);
			// `parse_in_arena`는 입력을 트리의 arena로 복사한다. 그래서 이 함수가
			// 끝난 뒤에도 스칼라 뷰가 유효하다 — 원문을 따로 붙잡을 필요가 없다.
			document.m_tree = ryml::parse_in_arena(ryml::csubstr(text.data(), text.size()));
			document.m_valid = (document.m_tree.size() > 0);
			if (!document.m_valid) error.assign("빈 문서");
			else error.clear();
		}
		catch (const std::exception& exception)
		{
			document.m_valid = false;
			error.assign(exception.what());
		}
		return document;
	}

	ParsedDocument ParsedDocument::ParseFile(ce::utf8_string_view utf8Path, ce::utf8_string& error)
	{
		if (utf8Path.find(ce::utf8_string_view("\0", 1)) != ce::utf8_string_view::npos)
		{
			error.assign("파일 경로에 NUL이 포함되어 있다");
			return {};
		}
		try
		{
			// char8_t marks the filesystem input as UTF-8 on Windows too. A narrow
			// std::string path would instead use the current native code page.
			std::u8string pathBytes(utf8Path.size(), u8'\0');
			if (!utf8Path.empty())
				std::memcpy(pathBytes.data(), utf8Path.data(), utf8Path.size());
			return ParseFilePath(std::filesystem::path(pathBytes), error);
		}
		catch (const std::exception& exception)
		{
			error.assign(exception.what());
			return {};
		}
	}

	ParsedDocument ParsedDocument::ParseFilePath(const std::filesystem::path& path, ce::utf8_string& error)
	{
		error.clear();
		try
		{
			const auto utf8Path = path.u8string();
			ce::utf8_string context;
			context.resize_and_overwrite(utf8Path.size(), [&](char* data, std::size_t length)
			{
				if (length != 0) std::memcpy(data, utf8Path.data(), length);
				return length;
			});
			if (context.find(ce::utf8_string_view("\0", 1)) != ce::utf8_string_view::npos)
			{
				error.assign("파일 경로에 NUL이 포함되어 있다");
				return {};
			}
			std::ifstream input(path, std::ios::binary);
			if (!input)
			{
				error.assign("파일을 열 수 없다: ");
				error.append(context.view());
				return {};
			}

			// A .asset may contain CEDO or CEMA, so the file buffer owns bytes,
			// not text. Only the YAML dispatch below gives it a UTF-8 text view.
			ce::dynamic_array<std::byte> bytes;
			ce::fixed_array<std::byte, 16384> chunk;
			while (input)
			{
				input.read(reinterpret_cast<char*>(chunk.data()),
					static_cast<std::streamsize>(chunk.size()));
				const auto count = static_cast<std::size_t>(input.gcount());
				bytes.insert(bytes.end(), chunk.data(), chunk.data() + count);
			}
			if (input.bad() || !input.eof())
			{
				error.assign("파일을 읽을 수 없다: ");
				error.append(context.view());
				return {};
			}
			const std::span<const std::byte> byteView{ bytes.data(), bytes.size() };
			if (IsCookedDocument(byteView)) return ParseCooked(byteView, error);
			return ParseTextWithContext(
				{ reinterpret_cast<const char*>(bytes.data()), bytes.size() }, error, context.view());
		}
		catch (const std::exception& exception)
		{
			error.assign(exception.what());
			return {};
		}
	}

	ParsedDocument ParsedDocument::ParseCooked(
		std::span<const std::byte> bytes, ce::utf8_string& error)
	{
		ParsedDocument document;
		// Adapt the cooked codec's existing standard-string diagnostic interface.
		std::string codecError;
		auto decoded = DecodeCookedDocument(bytes, codecError);
		error.assign({ codecError.data(), codecError.size() });
		if (!decoded) return document;
		document.m_tree = std::move(decoded->m_tree);
		document.m_valid = document.m_tree.size() > 0u;
		if (!document.m_valid) error.assign("빈 cooked document");
		return document;
	}

	ParsedDocument ParsedDocument::ParseText(const std::string& text, std::string& error)
	{
		ce::utf8_string diagnostic;
		auto document = ParseText({ text.data(), text.size() }, diagnostic);
		error.assign(diagnostic.data(), diagnostic.size());
		return document;
	}

	ParsedDocument ParsedDocument::ParseFile(const std::string& utf8Path, std::string& error)
	{
		ce::utf8_string diagnostic;
		auto document = ParseFile({ utf8Path.data(), utf8Path.size() }, diagnostic);
		error.assign(diagnostic.data(), diagnostic.size());
		return document;
	}

	ParsedDocument ParsedDocument::ParseFilePath(const std::filesystem::path& path, std::string& error)
	{
		ce::utf8_string diagnostic;
		auto document = ParseFilePath(path, diagnostic);
		error.assign(diagnostic.data(), diagnostic.size());
		return document;
	}

	ParsedDocument ParsedDocument::ParseCooked(std::span<const std::byte> bytes, std::string& error)
	{
		ce::utf8_string diagnostic;
		auto document = ParseCooked(bytes, diagnostic);
		error.assign(diagnostic.data(), diagnostic.size());
		return document;
	}

	ReadNode ParsedDocument::Root() const
	{
		if (!m_valid) return ReadNode{};
		return ReadNode::FromRyml(m_tree);
	}
}
