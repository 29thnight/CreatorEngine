#pragma once
#include <cstddef>
#include <cstdint>

// SerializationPlan D3-a-4 — 저작 노드의 **읽기 전용 뷰**.
//
// `Authoring::Document`가 소유를 감췄다면 이 타입은 **훅 인자**를 감춘다. 컴포넌트의
// `OnDeserialized(node)` 같은 훅은 리플렉션이 넘겨 주는 노드를 그 자리에서 읽기만
// 하는데, 인자 타입이 backend 노드라 컴포넌트 헤더가 포맷을 알아야 했다
// (§5 완료 기준 9가 막는 것이 바로 그것이다).
//
// ★ **뷰는 저장하지 않는다.** 이 객체는 호출 프레임 동안만 유효한 참조이며, 멤버로
//   보관하거나 훅 밖으로 내보내면 안 된다. 단일 backend인 ryml `NodeRef`는 `Tree`를
//   소유하지 않으므로 소유 문서가 먼저 죽으면 곧바로 dangling이 된다
//   (§8 "ryml view 수명 오용"). 장기 보관이 필요하면 `Document`다.
//
// ★ pimpl을 쓰지 않는 이유: 훅은 컴포넌트마다 매 로드마다 불린다. 호출당 힙 할당은
//   그 빈도에 맞지 않는다. 대신 불투명 포인터 하나를 들고, 실제 타입 복원은
//   `AuthoringNodeViewAccess.h`를 include한 직렬화 계층만 할 수 있게 한다 —
//   생성자가 private이고 Access가 유일한 friend라 임의 캐스팅이 성립하지 않는다.
namespace Authoring
{
	class NodeView
	{
	public:
		NodeView() = default;

		// 훅이 "노드가 왔는가"를 물을 수 있게 한다. 유효하지 않은 뷰는 backend의
		// "정의되지 않음"이 아니라 **뷰 자체가 비었음**을 뜻한다.
		[[nodiscard]] bool IsValid() const noexcept { return nullptr != m_first; }

	private:
		friend struct NodeViewAccess;

		// D3-b-4: 단일 backend의 `{tree, id}`를 불투명 두 워드로 보관한다.
		// ★ 그래도 **이 헤더는 포맷을 모른다.** 필드는 `const void*`와 정수뿐이고,
		//   의미 부여는 `AuthoringNodeViewAccess.h`(직렬화 계층)만 한다 —
		//   §5 완료 기준 9("Runtime interface에 Node 타입 0")를 지키는 방식이다.
		//
		NodeView(const void* first, std::size_t second) noexcept
			: m_first(first), m_second(second) {}

		const void* m_first{ nullptr };
		std::size_t m_second{ 0 };
	};

	// ── 쓰기 훅용 뷰 ─────────────────────────────────────────────────────────
	//
    // D3-b-2에서 읽기와 쓰기 훅을 타입으로 분리했고, D3-b-3부터 이 뷰는
    // `WriteNode`를 가리킨다. 읽기/쓰기 타입은 다시 합치지
	//   않는다. 훅의 의도(const read 대 mutable write)가 타입에서 보이고, writer가
	//   Tree를 소유하는 동안만 유효하다는 수명 계약도 서로 다르기 때문이다.
	class MutableNodeView
	{
	public:
		MutableNodeView() = default;

		[[nodiscard]] bool IsValid() const noexcept { return nullptr != m_node; }

	private:
		friend struct MutableNodeViewAccess;
		explicit MutableNodeView(void* node) noexcept : m_node(node) {}

		void* m_node{ nullptr };
	};
}
