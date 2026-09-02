#pragma once
#include "AuthoringNodeView.h"
#include "AuthoringReadNode.h"
#include "AuthoringWriteNode.h"

// SerializationPlan D3-a-4 — `NodeView`의 생성·해제 창구.
//
// `AuthoringDocumentAccess.h`와 같은 규율이다: 이 헤더를 include하는 TU가 직렬화
// 계층이고, 훅을 **선언하는** 헤더는 이것을 보지 않는다. 훅을 **구현하는** `.cpp`만
// include해서 노드를 되찾는다.
namespace Authoring
{
	struct NodeViewAccess
	{
		// 어댑터에서 단일 ryml 뷰를 만든다. 뷰는 소유하지 않으므로 임시 노드를
		// 타입으로 막는다.
		[[nodiscard]] static NodeView Make(const ReadNode& node) noexcept
		{
			if (!node) return {};
			return NodeView{ node.TreeForView(), node.IdForView() };
		}
		static NodeView Make(ReadNode&&) = delete;



		// 훅 구현이 뷰에서 노드를 되찾는다.
		//
		// ★ D3-b-2b-1b-2b: **어댑터를 값으로 돌려준다.** 이전에는 backend 노드
		//   참조였는데, 그러면 backend를 바꾸는 순간 훅 본문 전부가 함께 깨진다.
		//   어댑터를 돌려주면 훅 본문은 ReadNode 연산만 쓰게 되고, backend 교체가
		//   이 함수 한 줄로 좁혀진다.
		//
		// ★ 유효하지 않은 뷰에는 **비어 있는 노드**를 돌려준다. 훅이 IsValid()를
		//   잊어도 널 역참조로 죽지 않고 backend의 "없음"과 같은 값을 보게 된다 —
		//   기존 코드의 if (!node) 형태가 그대로 유지된다.
		[[nodiscard]] static ReadNode Node(const NodeView& view) noexcept
		{
			if (!view.IsValid()) return {};
			return ReadNode::FromRyml(
				*static_cast<const ryml::Tree*>(view.m_first), view.m_second);
		}

	private:
	};

	// `MutableNodeView`의 창구. 읽기 쪽과 **의도적으로 분리**돼 있다 — 하나가
	// 겸하면 읽기 backend를 옮기는 순간 쓰기 훅이 함께 끌려간다(D3-b-2 정정).
	struct MutableNodeViewAccess
	{
		[[nodiscard]] static MutableNodeView Make(WriteNode& node) noexcept
		{
			return MutableNodeView{ static_cast<void*>(&node) };
		}

		// 읽기 쪽과 같은 이유로 임시 노드를 막는다(§8 "ryml view 수명 오용").
		static MutableNodeView Make(WriteNode&&) = delete;

		// 유효하지 않은 뷰에는 no-op `WriteNode`를 돌려준다. WriteNode의 모든 변이
		// 연산은 invalid 상태에서 아무것도 하지 않으므로 널 역참조도, 버려지는
		// thread_local 상태의 누적도 없다.
		[[nodiscard]] static WriteNode Node(const MutableNodeView& view) noexcept
		{
			if (!view.IsValid()) return {};
			return *static_cast<WriteNode*>(ViewPointer(view));
		}

	private:
		[[nodiscard]] static void* ViewPointer(const MutableNodeView& view) noexcept
		{
			return view.m_node;
		}
	};
}
