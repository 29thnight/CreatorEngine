#pragma once

// SerializationPlan D3-a-4 — 저작 노드의 **읽기 전용 뷰**.
//
// `Authoring::Document`가 소유를 감췄다면 이 타입은 **훅 인자**를 감춘다. 컴포넌트의
// `OnDeserialized(node)` 같은 훅은 리플렉션이 넘겨 주는 노드를 그 자리에서 읽기만
// 하는데, 인자 타입이 backend 노드라 컴포넌트 헤더가 포맷을 알아야 했다
// (§5 완료 기준 9가 막는 것이 바로 그것이다).
//
// ★ **뷰는 저장하지 않는다.** 이 객체는 호출 프레임 동안만 유효한 참조이며, 멤버로
//   보관하거나 훅 밖으로 내보내면 안 된다. 지금 backend인 yaml-cpp에서는 어겨도
//   티가 안 나지만, D3-b의 ryml `NodeRef`는 `Tree`를 소유하지 않으므로 곧바로
//   dangling이 된다(§8 "ryml view 수명 오용"). 장기 보관이 필요하면 `Document`다.
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
		[[nodiscard]] bool IsValid() const noexcept { return nullptr != m_node; }

	private:
		friend struct NodeViewAccess;
		explicit NodeView(const void* node) noexcept : m_node(node) {}

		const void* m_node{ nullptr };
	};

	// ── 쓰기 훅용 뷰 ─────────────────────────────────────────────────────────
	//
	// D3-b-2가 **읽기 경로만** ryml로 옮긴다. 이득은 파싱에 있고 쓰기에는 없으므로,
	// 두 경로가 한동안 서로 다른 backend 위에서 산다. 그런데 `NodeView` 하나가
	// 읽기와 쓰기를 겸하고 있었기 때문에, 읽기의 backend를 바꾸면 쓰기 훅이 함께
	// 끌려가 D3-b-3를 앞당겨야 했다.
	//
	// 그래서 쓰기 쪽을 **타입으로** 갈라 둔다. 대상은 실측상 두 곳뿐이다
	// (`Entity::OnAfterSerialize` 훅 하나, 그 썽크 하나) — 분리 비용이 작고,
	// 그 대가로 두 경로가 독립적으로 움직일 수 있게 된다.
	//
	// ★ 이 뷰는 **저작 backend(yaml-cpp)를 계속 가리킨다.** D3-b-3가 쓰기 경로를
	//   옮길 때 이 타입의 backend가 바뀌고, 그때 두 타입이 다시 하나로 합쳐질지
	//   (읽기/쓰기 구분을 const로만 표현) 판단한다.
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
