#include "AuthoringAdapterParityProbe.h"

#include "AuthoringReadNode.h"
#include "AuthoringRymlErrorPolicy.h"

#include <algorithm>
#include <fstream>
#include <map>
#include <sstream>

namespace Authoring
{
	namespace
	{
		void Note(AdapterParityResult& result, const std::string& path,
			const std::string& where, const std::string& detail)
		{
			++result.divergences;
			if (result.firstDivergence.empty())
			{
				result.firstDivergence = path + " @ " + where + " : " + detail;
			}
		}

		std::string Bools(bool lhs, bool rhs)
		{
			return std::string("yamlcpp=") + (lhs ? "1" : "0") + " ryml=" + (rhs ? "1" : "0");
		}

		// 두 어댑터 노드를 같은 자리에서 비교하고 자식으로 내려간다.
		//
		// ★ 깊이 상한을 둔다. 순환은 YAML 앵커로 만들 수 있고, 그때 무한 재귀로
		//   죽으면 검사가 원인을 못 말해 준다.
		void Compare(const ReadNode& lhs, const ReadNode& rhs,
			const std::string& path, const std::string& where,
			AdapterParityResult& result, int depth)
		{
			if (depth > 64)
			{
				Note(result, path, where, "깊이 상한 64 초과");
				return;
			}

			++result.comparedNodes;

			const bool lv = static_cast<bool>(lhs);
			const bool rv = static_cast<bool>(rhs);
			if (lv != rv) { Note(result, path, where, "valid " + Bools(lv, rv)); return; }
			if (!lv) return;

			if (lhs.IsNull() != rhs.IsNull())
			{
				Note(result, path, where, "IsNull " + Bools(lhs.IsNull(), rhs.IsNull()));
			}
			if (lhs.IsScalar() != rhs.IsScalar())
			{
				Note(result, path, where, "IsScalar " + Bools(lhs.IsScalar(), rhs.IsScalar()));
			}
			if (lhs.IsMap() != rhs.IsMap())
			{
				Note(result, path, where, "IsMap " + Bools(lhs.IsMap(), rhs.IsMap()));
			}
			if (lhs.IsSequence() != rhs.IsSequence())
			{
				Note(result, path, where, "IsSequence " + Bools(lhs.IsSequence(), rhs.IsSequence()));
			}
			if (lhs.Size() != rhs.Size())
			{
				Note(result, path, where, "Size yamlcpp=" + std::to_string(lhs.Size())
					+ " ryml=" + std::to_string(rhs.Size()));
				return;
			}
			if (lhs.Scalar() != rhs.Scalar())
			{
				Note(result, path, where, "Scalar yamlcpp=\"" + std::string(lhs.Scalar())
					+ "\" ryml=\"" + std::string(rhs.Scalar()) + "\"");
			}

			if (lhs.IsSequence())
			{
				for (std::size_t i = 0; i < lhs.Size(); ++i)
				{
					Compare(lhs.At(i), rhs.At(i), path,
						where + "[" + std::to_string(i) + "]", result, depth + 1);
				}
				return;
			}

			if (!lhs.IsMap()) return;

			// ★ 맵은 키로 맞춘다 — 순서로 맞추면 두 backend의 순회 순서가 같다는
			//   **검증되지 않은 전제**에 기대게 된다. 키 집합이 같은지도 함께 본다.
			std::map<std::string, ReadNode> left;
			std::map<std::string, ReadNode> right;
			for (const auto entry : lhs.Map()) left.emplace(std::string(entry.key.Scalar()), entry.value);
			for (const auto entry : rhs.Map()) right.emplace(std::string(entry.key.Scalar()), entry.value);

			result.comparedMapEntries += left.size();

			if (left.size() != right.size())
			{
				Note(result, path, where, "맵 키 수 yamlcpp=" + std::to_string(left.size())
					+ " ryml=" + std::to_string(right.size()));
			}

			for (const auto& [key, value] : left)
			{
				const auto it = right.find(key);
				if (it == right.end())
				{
					Note(result, path, where, "ryml에 없는 키: " + key);
					continue;
				}
				Compare(value, it->second, path, where + "/" + key, result, depth + 1);

				// 키 조회 경로도 함께 본다 — 맵 순회가 맞아도 `operator[]`가
				// 다르면 소비자는 그쪽을 쓴다.
				const ReadNode byKeyLeft = lhs[key.c_str()];
				const ReadNode byKeyRight = rhs[key.c_str()];
				if (static_cast<bool>(byKeyLeft) != static_cast<bool>(byKeyRight))
				{
					Note(result, path, where, "operator[] 유효성 불일치: " + key);
				}
				if (lhs.HasChild(key.c_str()) != rhs.HasChild(key.c_str()))
				{
					Note(result, path, where, "HasChild 불일치: " + key);
				}
			}
		}
	}

	AdapterParityResult ProbeAdapterParity(const std::vector<std::string>& utf8Paths)
	{
		EnsureRymlErrorPolicy();

		AdapterParityResult result;
		for (const std::string& path : utf8Paths)
		{
			std::ifstream input(path, std::ios::binary);
			if (!input) continue;
			std::ostringstream buffer;
			buffer << input.rdbuf();
			const std::string text = buffer.str();

			// 모델 캐시 바이너리는 YAML이 아니다. 확장자로는 못 가른다.
			if (text.size() >= 4 && 0 == text.compare(0, 4, "CEMA"))
			{
				++result.skippedBinary;
				continue;
			}

			MetaYml::Node yamlRoot;
			bool yamlOk = false;
			try
			{
				yamlRoot = MetaYml::Load(text);
				yamlOk = true;
			}
			catch (const std::exception&) { yamlOk = false; }

			// ★ 트리는 이 스코프가 소유한다. 어댑터는 소유하지 않으므로 비교가
			//   끝날 때까지 살아 있어야 한다.
			ryml::Tree rymlTree;
			bool rymlOk = false;
			try
			{
				rymlTree = ryml::parse_in_arena(ryml::to_csubstr(text));
				rymlOk = (rymlTree.size() > 0);
			}
			catch (const std::exception&) { rymlOk = false; }

			if (yamlOk != rymlOk) { ++result.parseFailures; continue; }
			if (!yamlOk) continue;

			++result.files;
			Compare(ReadNode{ yamlRoot }, ReadNode::FromRyml(rymlTree), path, "", result, 0);
		}
		return result;
	}
}
