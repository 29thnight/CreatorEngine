#pragma once
#include "AuthoringNodeView.h"
#include "ReflectionYml.h"

// SerializationPlan D3-a-4 — `NodeView`의 생성·해제 창구.
//
// `AuthoringDocumentAccess.h`와 같은 규율이다: 이 헤더를 include하는 TU가 직렬화
// 계층이고, 훅을 **선언하는** 헤더는 이것을 보지 않는다. 훅을 **구현하는** `.cpp`만
// include해서 노드를 되찾는다.
namespace Authoring
{
	struct NodeViewAccess
	{
		// 리플렉션 썽크가 훅에 넘길 뷰를 만든다. 넘긴 노드는 훅이 반환할 때까지
		// 살아 있어야 한다 — 뷰는 소유하지 않는다.
		//
		// ★ **`iterator_value`를 넘기면 base로 슬라이싱된다.** yaml-cpp의 맵 순회는
		//   `Node`에서 파생된 `detail::iterator_value`를 주는데, 이 함수는 `Node`
		//   참조로 받으므로 뷰가 가리키는 것은 그 base 부분이다. `Node` 연산
		//   (`operator[]`·`IsSequence`·`as<T>`)만 쓰는 한 안전하고, D3-a-5가 옮긴
		//   `LoadComponent`·`DesirealizeGameObject`가 실제로 그렇다. 그러나 파생
		//   고유 멤버(`first`/`second`)가 필요해지면 **이 창구로는 못 얻는다** —
		//   그때는 뷰가 아니라 그 자리에서 노드를 직접 받아야 한다.
		[[nodiscard]] static NodeView Make(const MetaYml::Node& node) noexcept
		{
			return NodeView{ static_cast<const void*>(&node) };
		}

		// ★ 임시 노드로 뷰를 만드는 것을 **타입으로** 막는다.
		//
		//   `Make(node["key"])`처럼 rvalue를 넘기면 뷰가 그 임시를 가리킨다. 지금은
		//   전체 표현식이 끝날 때까지 임시가 살아 있어 "호출 인자로만 쓰면" 우연히
		//   안전하지만, 그 우연은 뷰를 한 번만 더 전달하면 깨진다. §8의 "ryml view
		//   수명 오용"이 정확히 이 형태이므로, 주석으로 경고하는 대신 컴파일되지
		//   않게 한다 — 호출부는 named 변수를 두면 된다.
		static NodeView Make(MetaYml::Node&&) = delete;

		// 훅 구현이 뷰에서 노드를 되찾는다.
		//
		// ★ 유효하지 않은 뷰에 대해서는 **정의되지 않은 노드**를 돌려준다. 훅이
		//   `IsValid()`를 잊어도 널 역참조로 죽지 않고 backend의 "없음"과 같은 값을
		//   보게 된다 — 기존 코드가 `if (!node)` 로 시작하는 형태를 그대로 유지할 수
		//   있다는 뜻이다.
		[[nodiscard]] static const MetaYml::Node& Node(const NodeView& view) noexcept
		{
			static const MetaYml::Node kUndefined{};
			if (!view.IsValid()) return kUndefined;
			return *static_cast<const MetaYml::Node*>(ViewPointer(view));
		}

	private:
		[[nodiscard]] static const void* ViewPointer(const NodeView& view) noexcept
		{
			return view.m_node;
		}
	};

	// `MutableNodeView`의 창구. 읽기 쪽과 **의도적으로 분리**돼 있다 — 하나가
	// 겸하면 읽기 backend를 옮기는 순간 쓰기 훅이 함께 끌려간다(D3-b-2 정정).
	struct MutableNodeViewAccess
	{
		[[nodiscard]] static MutableNodeView Make(MetaYml::Node& node) noexcept
		{
			return MutableNodeView{ static_cast<void*>(&node) };
		}

		// 읽기 쪽과 같은 이유로 임시 노드를 막는다(§8 "ryml view 수명 오용").
		static MutableNodeView Make(MetaYml::Node&&) = delete;

		// ★ 유효하지 않은 뷰에는 **버려지는 노드**를 돌려준다. 널 역참조로 죽지
		//   않되, 훅이 쓴 값은 아무 데도 가지 않는다. 읽기 쪽의 `kUndefined`와
		//   달리 `static`이 아닌 이유는 쓰기 대상이라 상태가 누적되면 안 되기
		//   때문이다 — 대신 thread_local로 두어 스레드 간 오염도 막는다.
		[[nodiscard]] static MetaYml::Node& Node(const MutableNodeView& view) noexcept
		{
			thread_local MetaYml::Node discarded{};
			if (!view.IsValid())
			{
				discarded = MetaYml::Node{};
				return discarded;
			}
			return *static_cast<MetaYml::Node*>(ViewPointer(view));
		}

	private:
		[[nodiscard]] static void* ViewPointer(const MutableNodeView& view) noexcept
		{
			return view.m_node;
		}
	};
}
