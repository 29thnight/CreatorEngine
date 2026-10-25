#include "AuthoringNodeEquality.h"

namespace
{
	// 깊이 상한. 저작 노드는 자기 참조를 만들 수 없지만, 손으로 편집한 파일이나
	// 앵커/별칭이 섞인 문서에서 병적으로 깊은 트리가 들어오면 스택을 먹는다.
	// 상한에 닿으면 "같지 않다"로 답한다 — 오버라이드 시딩에서 이는 "값이 다르니
	// 기록한다"는 뜻이라 안전한 쪽으로 틀린다(누락보다 과잉이 낫다).
	constexpr int kMaxDepth = 64;

	bool NodesEqualImpl(const Authoring::ReadNode& lhs,
		const Authoring::ReadNode& rhs, int depth)
	{
		if (depth >= kMaxDepth) return false;

		// 정의되지 않은 노드와 널은 "값 없음"으로 같게 본다. ryml emitter는 `~`와
		// `null`의 원래 표기를 보존할 수 있으므로 Dump 문자열보다 이 계약이 우선한다.
		const bool lhsEmpty = !lhs || lhs.IsNull();
		const bool rhsEmpty = !rhs || rhs.IsNull();
		if (lhsEmpty || rhsEmpty) return lhsEmpty && rhsEmpty;

		if (lhs.IsScalar() || rhs.IsScalar())
		{
			if (!lhs.IsScalar() || !rhs.IsScalar()) return false;
			// 타입 없는 YAML에서 스칼라의 정본은 문자열이다. `1`과 `1.0`은 다른
			// 스칼라이며, Dump 비교도 그렇게 판정했다 — 여기서 수치로 정규화하면
			// 저작자가 적은 표기를 지워 버린다.
			return lhs.AsString() == rhs.AsString();
		}

		if (lhs.IsSequence() || rhs.IsSequence())
		{
			if (!lhs.IsSequence() || !rhs.IsSequence()
				|| lhs.Size() != rhs.Size()) return false;
			for (std::size_t i = 0; i < lhs.Size(); ++i)
			{
				if (!NodesEqualImpl(lhs.At(i), rhs.At(i), depth + 1)) return false;
			}
			return true;
		}

		if (lhs.IsMap() || rhs.IsMap())
		{
			if (!lhs.IsMap() || !rhs.IsMap() || lhs.Size() != rhs.Size())
				return false;
			// 크기가 같으므로 한쪽 키가 모두 반대쪽에 있고 값이 같으면 충분하다
			// (중복 키는 parser가 파싱 단계에서 접는다).
			for (const Authoring::MapEntry entry : lhs.Map())
			{
				if (!entry.key.IsScalar()) return false;
				const std::string key = entry.key.AsString();
				const Authoring::ReadNode counterpart = rhs[key.c_str()];
				if (!counterpart) return false;
				if (!NodesEqualImpl(entry.value, counterpart, depth + 1)) return false;
			}
			return true;
		}

		return false;
	}
}

namespace Authoring
{
	bool NodesEqual(const ReadNode& lhs, const ReadNode& rhs)
	{
		return NodesEqualImpl(lhs, rhs, 0);
	}
}
