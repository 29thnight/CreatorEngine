#pragma once
#include "AuthoringDocument.h"
#include "ReflectionYml.h"

// SerializationPlan D3-a-3 — 저작 문서의 **소비** 창구.
//
// 이 헤더를 include하는 순간 그 TU는 backend 포맷을 안다. 그래서 include하는 곳이
// 곧 "직렬화 계층"의 경계다 — 소유자의 헤더(`SceneManager.h` 등)는 이것을 include하지
// 않고 `AuthoringDocument.h`만 본다.
//
// ★ 반환되는 노드 참조는 **문서보다 오래 살 수 없다.** 지금 backend인 yaml-cpp는
//   Node가 값 의미론이라 이 규칙을 어겨도 티가 안 나지만, D3-b의 ryml에서는
//   `NodeRef`가 `Tree`를 소유하지 않으므로 곧바로 dangling이 된다(§8 리스크
//   "ryml view 수명 오용"). 참조를 저장하지 말고 그 자리에서 쓴다.
namespace Authoring
{
	struct DocumentAccess
	{
		// 노드를 문서의 소유로 넘긴다. 호출자는 넘긴 노드를 더 쓰지 않는다.
		[[nodiscard]] static Document Adopt(MetaYml::Node node);

		[[nodiscard]] static const MetaYml::Node& Node(const Document& document);
		[[nodiscard]] static MetaYml::Node& Node(Document& document);
	};
}
