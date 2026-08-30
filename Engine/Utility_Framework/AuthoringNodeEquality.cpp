#include "AuthoringNodeEquality.h"

namespace
{
	// 깊이 상한. 저작 노드는 자기 참조를 만들 수 없지만, 손으로 편집한 파일이나
	// 앵커/별칭이 섞인 문서에서 병적으로 깊은 트리가 들어오면 스택을 먹는다.
	// 상한에 닿으면 "같지 않다"로 답한다 — 오버라이드 시딩에서 이는 "값이 다르니
	// 기록한다"는 뜻이라 안전한 쪽으로 틀린다(누락보다 과잉이 낫다).
	constexpr int kMaxDepth = 64;

	bool NodesEqualImpl(const MetaYml::Node& lhs, const MetaYml::Node& rhs, int depth)
	{
		if (depth >= kMaxDepth) return false;

		// 정의되지 않은 노드와 널은 "값 없음"으로 같게 본다. Dump 비교도 그랬다
		// (둘 다 "~" 또는 빈 문자열로 떨어진다).
		const bool lhsEmpty = !lhs.IsDefined() || lhs.IsNull();
		const bool rhsEmpty = !rhs.IsDefined() || rhs.IsNull();
		if (lhsEmpty || rhsEmpty) return lhsEmpty && rhsEmpty;

		if (lhs.Type() != rhs.Type()) return false;

		switch (lhs.Type())
		{
		case MetaYml::NodeType::Scalar:
			// 타입 없는 YAML에서 스칼라의 정본은 문자열이다. `1`과 `1.0`은 다른
			// 스칼라이며, Dump 비교도 그렇게 판정했다 — 여기서 수치로 정규화하면
			// 저작자가 적은 표기를 지워 버린다.
			return lhs.Scalar() == rhs.Scalar();

		case MetaYml::NodeType::Sequence:
		{
			if (lhs.size() != rhs.size()) return false;
			for (std::size_t i = 0; i < lhs.size(); ++i)
			{
				if (!NodesEqualImpl(lhs[i], rhs[i], depth + 1)) return false;
			}
			return true;
		}

		case MetaYml::NodeType::Map:
		{
			if (lhs.size() != rhs.size()) return false;
			// 크기가 같으므로 한쪽 키가 모두 반대쪽에 있고 값이 같으면 충분하다
			// (중복 키는 yaml-cpp가 파싱 단계에서 접는다).
			for (const auto& entry : lhs)
			{
				// ★ 키 Node를 그대로 `rhs[key]`에 넣으면 안 된다. yaml-cpp의 Node
				//   인덱싱은 그 경우 **노드 identity**로 찾기 때문에, 같은 문자열
				//   키라도 다른 문서에서 온 노드면 못 찾는다. 자가 검사의
				//   map-key-order/map-style/nested-key-order 세 항목이 이 결함을
				//   잡아냈다 — 순서 무시가 통째로 동작하지 않고 있었다.
				const MetaYml::Node& keyNode = entry.first;
				const MetaYml::Node counterpart = keyNode.IsScalar()
					? rhs[keyNode.Scalar()]   // 스칼라 키는 문자열로 조회한다
					: rhs[keyNode];           // 복합 키는 backend 규칙에 맡긴다
				if (!counterpart.IsDefined()) return false;
				if (!NodesEqualImpl(entry.second, counterpart, depth + 1)) return false;
			}
			return true;
		}

		default:
			break;
		}

		return false;
	}
}

namespace Authoring
{
	bool NodesEqual(const MetaYml::Node& lhs, const MetaYml::Node& rhs)
	{
		return NodesEqualImpl(lhs, rhs, 0);
	}
}
