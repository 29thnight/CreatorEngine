#pragma once
#include <memory>

// SerializationPlan D3-a-3 — 저작 문서의 **소유권** 타입.
//
// §3.3이 요구하는 것은 두 가지의 분리다: 문서를 **소유하는 것**과 그 안을 **탐색하는
// 것**. 지금까지 장기 보관 지점들은 backend 노드를 값으로 들고 있었고, 그래서
// 소유자의 헤더가 포맷 타입을 알아야 했다.
//
// 이 타입은 backend를 숨긴다(pimpl). 헤더에 YAML/ryml 타입이 없으므로 이 문서를
// 멤버로 갖는 클래스의 헤더도 포맷을 모른다. 실제 노드 접근은 직렬화 계층만
// `AuthoringDocumentAccess.h`를 include해서 얻는다 — 소비 정책이 헤더가 아니라
// include 경계로 표현된다.
//
// ★ **복사 금지, 이동만 가능.** 문서는 소유이고, 값 복사가 조용히 일어나면 "같은
//   문서를 둘이 들고 있다"가 된다. D3-b가 backend를 ryml로 바꾸면 `Tree`는 실제로
//   무겁고 `NodeRef`는 트리를 소유하지 않으므로, 그때 복사 의미론이 남아 있으면
//   dangling view를 만든다. 지금 막아 둔다.
//
// D3-b에서 바뀌는 것은 `Impl` 하나이며 이 헤더와 소유자들의 코드는 그대로다.
namespace Authoring
{
	class Document
	{
	public:
		Document();
		~Document();

		Document(Document&&) noexcept;
		Document& operator=(Document&&) noexcept;

		Document(const Document&) = delete;
		Document& operator=(const Document&) = delete;

		// 문서가 아무 내용도 담지 않은 상태인가. backend의 "정의되지 않음"에 대응한다.
		[[nodiscard]] bool IsEmpty() const noexcept;

		// 내용을 버리고 빈 문서로 되돌린다.
		void Clear() noexcept;

	private:
		friend struct DocumentAccess;
		struct Impl;
		std::unique_ptr<Impl> m_impl;
	};
}
