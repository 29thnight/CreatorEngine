#pragma once
#include "AuthoringNodeView.h"
#include "AuthoringReadNode.h"

// SerializationPlan D3-a-4 — `NodeView`의 생성·해제 창구.
//
// `AuthoringDocumentAccess.h`와 같은 규율이다: 이 헤더를 include하는 TU가 직렬화
// 계층이고, 훅을 **선언하는** 헤더는 이것을 보지 않는다. 훅을 **구현하는** `.cpp`만
// include해서 노드를 되찾는다.
namespace Authoring
{
	struct NodeViewAccess
	{
		// 뷰 표현의 태그. 0은 무효, 1은 yaml-cpp 노드 포인터, 2는 ryml {트리,id}.
		static constexpr std::uint8_t kYamlCpp = 1;
		static constexpr std::uint8_t kRyml = 2;

		// backend 노드에서 뷰를 만든다(아직 옮기지 않은 호출부용).
		[[nodiscard]] static NodeView Make(const MetaYml::Node& node) noexcept
		{
			return NodeView{ static_cast<const void*>(&node), 0, kYamlCpp };
		}
		// 임시 노드로 뷰를 만드는 것을 **타입으로** 막는다 — 뷰는 소유하지 않으므로
		// 임시를 가리키면 전달 한 번에 dangling이 된다(§8 ryml view 수명 오용).
		static NodeView Make(MetaYml::Node&&) = delete;

		// ★ 어댑터에서 뷰를 만든다. **두 backend를 모두 담는다** — 뷰가 한쪽에만
		//   묶여 있던 것이 씬 전환을 막던 블로커였다.
		[[nodiscard]] static NodeView Make(const ReadNode& node) noexcept
		{
			if (node.IsRymlBacked())
			{
				return NodeView{ node.RymlTreeDuringTransition(),
					node.RymlIdDuringTransition(), kRyml };
			}
			return NodeView{ static_cast<const void*>(&node.BackendNodeDuringTransition()),
				0, kYamlCpp };
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
			switch (view.m_kind)
			{
			case kYamlCpp:
				return ReadNode{ *static_cast<const MetaYml::Node*>(view.m_first) };
			case kRyml:
				return ReadNode::FromRyml(
					*static_cast<const ryml::Tree*>(view.m_first), view.m_second);
			default:
				return ReadNode{};
			}
		}

	private:
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
