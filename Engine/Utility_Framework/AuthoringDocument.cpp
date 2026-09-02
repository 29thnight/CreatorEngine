#include "AuthoringDocumentAccess.h"

namespace Authoring
{
	// backend는 여기 한 곳에만 있다. D3-b는 이 구조체의 멤버 타입만 바꾼다.
	struct Document::Impl
	{
		// Document는 SceneManager 같은 전역 서비스의 멤버로 정적 초기화될 수
		// 있다. WriteDocument 생성은 ryml 전역 callback 정책을 설치하므로 main()
		// 이전에 만들면 라이브러리 내부 정적 상태와 초기화 순서가 충돌한다.
		// 실제 문서가 필요할 때만 backend 소유자를 만든다.
		std::unique_ptr<WriteDocument> document{};
	};

	Document::Document()
		: m_impl(std::make_unique<Impl>())
	{
	}

	// pimpl의 소멸자는 Impl이 완전한 타입인 이 TU에 있어야 한다.
	Document::~Document() = default;

	Document::Document(Document&&) noexcept = default;
	Document& Document::operator=(Document&&) noexcept = default;

	bool Document::IsEmpty() const noexcept
	{
		// 이동 이후의 문서는 impl이 비어 있을 수 있다. 그 상태도 "빈 문서"다.
		if (!m_impl) return true;
		return !m_impl->document || m_impl->document->IsEmpty();
	}

	void Document::Clear() noexcept
	{
		if (!m_impl) return;
		m_impl->document.reset();
	}

	Document DocumentAccess::Adopt(WriteDocument writer)
	{
		Document document;
		document.m_impl->document =
			std::make_unique<WriteDocument>(std::move(writer));
		return document;
	}

	Document DocumentAccess::Clone(const ReadNode& node)
	{
		WriteDocument writer;
		writer.Root().Assign(node);
		return Adopt(std::move(writer));
	}

	ReadNode DocumentAccess::Read(const Document& document)
	{
		if (!document.m_impl || !document.m_impl->document) return {};
		return static_cast<const WriteDocument&>(*document.m_impl->document).Root();
	}

	WriteNode DocumentAccess::Write(Document& document)
	{
		if (!document.m_impl) document.m_impl = std::make_unique<Document::Impl>();
		if (!document.m_impl->document)
			document.m_impl->document = std::make_unique<WriteDocument>();
		return document.m_impl->document->Root();
	}
}
