#include "AuthoringParserProbe.h"

#include "ReflectionYml.h"
#include "AuthoringRymlErrorPolicy.h"

#include <ryml/ryml.hpp>
#include <ryml/ryml_std.hpp>

#include <chrono>
#include <fstream>
#include <sstream>

namespace
{
	using Clock = std::chrono::steady_clock;

	// ryml 에러 정책(abort → 예외)은 D3-b-1에서
	// `AuthoringRymlErrorPolicy`로 옮겼다. 프로브가 사본을 들고 있으면
	// 제품 경로와 프로브가 서로 다른 정책으로 갈라질 수 있고,
	// 그러면 프로브가 통과해도 제품이 abort하는 상태를 몸라 수 있다.
	// 정본은 하나다.

	std::uint64_t ElapsedNanos(Clock::time_point start, Clock::time_point end)
	{
		return static_cast<std::uint64_t>(
			std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count());
	}

	bool ReadFileUtf8(const std::string& path, std::string& out)
	{
		std::ifstream stream(path, std::ios::binary);
		if (!stream) return false;
		std::ostringstream buffer;
		buffer << stream.rdbuf();
		out = buffer.str();
		return true;
	}

	std::string RymlToString(const ryml::ConstNodeRef& node)
	{
		if (!node.has_val()) return {};
		const ryml::csubstr value = node.val();
		return std::string(value.str, value.len);
	}

	// 두 트리를 같은 규칙으로 훑는다. 규칙은 `Authoring::NodesEqual`과 같다 —
	// 스칼라는 문자열, 시퀀스는 순서 포함, 맵은 키 집합과 값(순서 무시).
	bool CompareTrees(const MetaYml::Node& lhs, const ryml::ConstNodeRef& rhs,
		const std::string& path, std::string& outDifference, std::uint64_t& counted)
	{
		++counted;

		const bool lhsEmpty = !lhs.IsDefined() || lhs.IsNull();

		// ★ ryml의 널 판정은 `val_is_null()`이다. 처음에는 `val() == nullptr`로 봤는데,
		//   저작 문서의 `key: ~`는 ryml에서 **값이 있는 스칼라**로 들어오므로 그 조건이
		//   거짓이 되고, yaml-cpp만 널로 읽어 "한쪽만 비어 있다"가 23건 나왔다.
		//   파서 차이가 아니라 **비교기의 비대칭**이었다.
		//   `val_is_null()`은 `has_val()`이 참일 때만 부를 수 있다(내부 assert).
		const bool rhsEmpty = rhs.invalid()
			|| (rhs.has_val() && rhs.val_is_null())
			|| (!rhs.has_val() && !rhs.is_seq() && !rhs.is_map());
		if (lhsEmpty || rhsEmpty)
		{
			if (lhsEmpty && rhsEmpty) return true;
			outDifference = path + ": 한쪽만 비어 있다";
			return false;
		}

		if (lhs.IsScalar())
		{
			if (!rhs.has_val())
			{
				outDifference = path + ": yaml-cpp는 스칼라, ryml은 아니다";
				return false;
			}
			const std::string rhsValue = RymlToString(rhs);
			if (lhs.Scalar() != rhsValue)
			{
				outDifference = path + ": 스칼라 불일치 ['" + lhs.Scalar() + "'] vs ['" + rhsValue + "']";
				return false;
			}
			return true;
		}

		if (lhs.IsSequence())
		{
			if (!rhs.is_seq())
			{
				outDifference = path + ": yaml-cpp는 시퀀스, ryml은 아니다";
				return false;
			}
			if (lhs.size() != rhs.num_children())
			{
				outDifference = path + ": 시퀀스 길이 " + std::to_string(lhs.size())
					+ " vs " + std::to_string(rhs.num_children());
				return false;
			}
			for (std::size_t i = 0; i < lhs.size(); ++i)
			{
				if (!CompareTrees(lhs[i], rhs[i], path + "[" + std::to_string(i) + "]",
					outDifference, counted))
					return false;
			}
			return true;
		}

		if (lhs.IsMap())
		{
			if (!rhs.is_map())
			{
				outDifference = path + ": yaml-cpp는 맵, ryml은 아니다";
				return false;
			}
			if (lhs.size() != rhs.num_children())
			{
				outDifference = path + ": 맵 크기 " + std::to_string(lhs.size())
					+ " vs " + std::to_string(rhs.num_children());
				return false;
			}
			for (const auto& entry : lhs)
			{
				if (!entry.first.IsScalar())
				{
					// 복합 키는 이 프로브의 범위 밖이다. 저작 문서에는 없지만,
					// 있다면 "확인하지 못했다"를 통과로 읽지 않도록 실패로 낸다.
					outDifference = path + ": 복합 키는 대조하지 않는다";
					return false;
				}
				const std::string key = entry.first.Scalar();
				const ryml::ConstNodeRef child = rhs.find_child(ryml::to_csubstr(key));
				if (child.invalid())
				{
					outDifference = path + "/" + key + ": ryml에 키가 없다";
					return false;
				}
				if (!CompareTrees(entry.second, child, path + "/" + key, outDifference, counted))
					return false;
			}
			return true;
		}

		outDifference = path + ": 알 수 없는 노드 종류";
		return false;
	}
}

namespace Authoring
{
	ParserProbeResult ProbeParsers(const std::string& utf8Path)
	{
		ParserProbeResult result{};

		std::string text;
		if (!ReadFileUtf8(utf8Path, text)) return result;

		// ★ `.asset`은 두 포맷을 담는다: 재질은 평문 YAML, 모델 캐시는 `CEMA` 매직으로
		//   시작하는 바이너리다. 확장자만 보고 파서에 넣으면 yaml-cpp는 쓰레기를
		//   만들어 내고 ryml은 프로세스를 죽인다(실측: 14개 전부 모델 캐시였다).
		if (text.size() >= 4 && 0 == text.compare(0, 4, "CEMA"))
		{
			result.skippedBinaryAsset = true;
			return result;
		}

		// yaml-cpp
		MetaYml::Node yamlRoot;
		{
			const auto start = Clock::now();
			try
			{
				yamlRoot = MetaYml::Load(text);
				result.parsedByYamlCpp = true;
			}
			catch (const std::exception&)
			{
				result.parsedByYamlCpp = false;
			}
			result.yamlCppNanoseconds = ElapsedNanos(start, Clock::now());
		}

		// ryml — 장기 보관이 아니라 즉시 대조용이므로 arena 파싱으로 트리를 소유한다.
		//
		// ★ 여기서 CR을 지우는 이유가 D3-b-1에서 **정정됐다.**
		//   원래 주석은 "ryml은 CRLF를 만나면 abort하므로 정규화가 전제"였다.
		//   14종을 각각 태워 보니 **CRLF는 ryml 0.16이 정상 파싱한다** — 단순 맵·
		//   시퀀스·블록 스칼라·folded·주석 모두 통과했다. 실제 트리거는 **홀로 선
		//   CR**이다(`check failed: rem.find(CR) == npos`). 관찰한 abort 메시지를
		//   재 보지 않고 인접한 사실(자산이 CRLF였다)에 붙인 오진이었다.
		//
		//   그러므로 이 정규화는 **정확성을 위해 필요한 것이 아니라, 홀로 선 CR이
		//   섞인 파일에서도 대조를 끝까지 돌리기 위한 방어**다.
		//
		// ★ 정규화 시간을 ryml 몫에 포함한다. ryml의 장점은 "입력 버퍼를 그대로
		//   쓴다"는 것인데 정규화가 사본을 강제하므로, 그 비용을 빼고 재면 실제
		//   전환에서 얻을 수 없는 수치를 보고하게 된다.
		ryml::Tree rymlTree;
		{
			Authoring::EnsureRymlErrorPolicy();
			const auto start = Clock::now();

			std::string normalized;
			normalized.reserve(text.size());
			for (const char c : text)
			{
				if (c != '\r') normalized.push_back(c);
			}
			result.normalizedCrLf = (normalized.size() != text.size());

			try
			{
				rymlTree = ryml::parse_in_arena(ryml::to_csubstr(normalized));
				result.parsedByRyml = (rymlTree.size() > 0);
			}
			catch (const std::exception& exception)
			{
				result.parsedByRyml = false;
				result.rymlError = exception.what();
			}
			result.rymlNanoseconds = ElapsedNanos(start, Clock::now());
		}

		if (!result.parsedByYamlCpp || !result.parsedByRyml) return result;

		const ryml::ConstNodeRef rymlRoot = rymlTree.rootref();
		result.structurallyEqual = CompareTrees(yamlRoot, rymlRoot, "",
			result.firstDifference, result.comparedNodes);
		return result;
	}
}
