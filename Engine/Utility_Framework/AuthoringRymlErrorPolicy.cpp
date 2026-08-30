#include "AuthoringRymlErrorPolicy.h"

#include <mutex>
#include <stdexcept>

#include <ryml/ryml.hpp>
#include <ryml/ryml_std.hpp>

namespace Authoring
{
	namespace
	{
		std::once_flag g_installOnce;

		// 메시지 앞에 **채널 이름**을 붙인다. ryml은 에러를 basic/parse/visit
		// 셋으로 나누고, 하나만 덮으면 나머지는 여전히 abort한다. 그런데
		// what() 문자열만 보면 어느 채널을 타고 왔는지 알 수 없어서,
		// 게이트가 "둘 다 덮였다"를 단정하지 못한다. 태그가 그것을 가능하게 한다.
		[[noreturn]] void ThrowFromRyml(const char* channel, ryml::csubstr message)
		{
			// 메시지가 비어 오는 경우가 있다(내부 assert 경로). 빈 what()은
			// 진단에서 "예외가 안 왔다"와 구분되지 않으므로 자리표를 채운다.
			std::string text = "[";
			text += channel;
			text += "] ";
			if (nullptr == message.str || 0 == message.len)
			{
				text += "(no message)";
			}
			else
			{
				text.append(message.str, message.len);
			}
			throw std::runtime_error(text);
		}

		void Install()
		{
			// ryml 0.16은 에러 콜백을 basic/parse/visit 셋으로 나눈다. **하나만
			// 덮으면 나머지 경로에서 여전히 abort한다** — 실측으로 만난 두 실패가
			// 각각 basic(CRLF)과 parse(멀티라인 키)였다. 셋을 모두 덮는다.
			ryml::Callbacks callbacks = ryml::get_callbacks();
			callbacks.m_error_basic = [](ryml::csubstr msg,
				ryml::ErrorDataBasic const&, void*)
			{
				ThrowFromRyml("basic", msg);
			};
			callbacks.m_error_parse = [](ryml::csubstr msg,
				ryml::ErrorDataParse const&, void*)
			{
				ThrowFromRyml("parse", msg);
			};
			callbacks.m_error_visit = [](ryml::csubstr msg,
				ryml::ErrorDataVisit const&, void*)
			{
				ThrowFromRyml("visit", msg);
			};

			// 할당자 콜백은 **건드리지 않는다.** ryml 기본 할당자를 그대로 두는
			// 것이 여기서는 옳다 — 이 저장소는 전역 operator new 교체가 yaml-cpp
			// 내부 할당에서 터져 철회된 이력이 있다(메모리 계층 3중). 파서를
			// 옮기면서 할당자까지 함께 바꾸면 실패가 섞여 원인을 못 가른다.
			ryml::set_callbacks(callbacks);
		}
	}

	void EnsureRymlErrorPolicy()
	{
		std::call_once(g_installOnce, &Install);
	}

	RymlErrorPolicyProbe ProbeRymlErrorPolicy()
	{
		EnsureRymlErrorPolicy();

		RymlErrorPolicyProbe probe;
		std::string loneCrChannel;
		std::string tabChannel;

		const auto channelOf = [](const std::string& message) -> std::string
		{
			// 메시지는 "[basic] ..." 꼴이다. 태그가 없으면 빈 문자열을 돌려
			// "채널을 알 수 없음"을 통과로 읽지 않게 한다.
			if (message.size() < 2 || message[0] != '[') return {};
			const std::size_t close = message.find(']');
			if (std::string::npos == close) return {};
			return message.substr(1, close - 1);
		};

		const auto record = [&probe](const std::exception& exception)
		{
			if (probe.firstMessage.empty()) probe.firstMessage = exception.what();
		};

		// ── basic 채널: 홀로 선 \r ──────────────────────────────────────────
		//
		// 옛 Mac 개행이거나 \rLF가 반쯤 깨진 파일이다. 외부 도구·수기 편집·
		// 잘못된 병합으로 실제로 들어온다. ryml은 여기서 abort한다.
		try
		{
			const ryml::Tree tree = ryml::parse_in_arena(
				ryml::to_csubstr("root:\r  key: value"));
			(void)tree;
		}
		catch (const std::exception& exception)
		{
			probe.threwOnLoneCr = true;
			loneCrChannel = channelOf(exception.what());
			record(exception);
		}

		// ── parse 채널: 탭 들여쓰기 ─────────────────────────────────────────
		//
		// YAML 명세가 금지하는 형태이고 에디터 설정 하나로 쉽게 만들어진다.
		try
		{
			const ryml::Tree tree = ryml::parse_in_arena(
				ryml::to_csubstr("root:\n\tkey: value\n"));
			(void)tree;
		}
		catch (const std::exception& exception)
		{
			probe.threwOnTabIndent = true;
			tabChannel = channelOf(exception.what());
			record(exception);
		}

		probe.coveredDistinctChannels =
			!loneCrChannel.empty() && !tabChannel.empty() && (loneCrChannel != tabChannel);

		// ── 대조군 1: 정상 문서는 여전히 읽힌다 ─────────────────────────────
		try
		{
			const ryml::Tree tree = ryml::parse_in_arena(
				ryml::to_csubstr("root:\n  key: value\n  list:\n    - 1\n    - 2\n"));
			probe.parsedValidDocument =
				(tree.size() > 0) && tree.rootref().has_child("root");
		}
		catch (const std::exception&)
		{
			probe.parsedValidDocument = false;
		}

		// ── 대조군 2: \rLF는 정상 파싱된다 ──────────────────────────────────
		//
		// 이 단정이 깨지면 ryml의 개행 처리가 바뀐 것이고, 그러면 파싱 전 정규화
		// 사본이 다시 필요해진다 — D3-b의 성능 계산이 달라지는 지점이다.
		try
		{
			const ryml::Tree tree = ryml::parse_in_arena(
				ryml::to_csubstr("root:\r\n  key: value\r\n  list:\r\n    - 1\r\n    - 2\r\n"));
			probe.parsedCrLfDocument =
				(tree.size() > 0) && tree.rootref().has_child("root");
		}
		catch (const std::exception&)
		{
			probe.parsedCrLfDocument = false;
		}

		return probe;
	}

	RymlParseAttempt TryParseWithPolicy(const std::string& text)
	{
		EnsureRymlErrorPolicy();

		RymlParseAttempt attempt;
		try
		{
			const ryml::Tree tree = ryml::parse_in_arena(
				ryml::csubstr(text.data(), text.size()));
			attempt.nodeCount = static_cast<std::uint64_t>(tree.size());
			attempt.parsed = true;
		}
		catch (const std::exception& exception)
		{
			attempt.threw = true;
			attempt.message = exception.what();
			if (attempt.message.empty()) attempt.message = "(empty what())";
		}
		return attempt;
	}
}
