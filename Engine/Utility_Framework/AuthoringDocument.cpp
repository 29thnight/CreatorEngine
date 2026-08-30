#include "AuthoringDocumentAccess.h"

namespace Authoring
{
	// backend는 여기 한 곳에만 있다. D3-b는 이 구조체의 멤버 타입만 바꾼다.
	struct Document::Impl
	{
		MetaYml::Node node{};
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
		return !m_impl->node.IsDefined();
	}

	void Document::Clear() noexcept
	{
		if (!m_impl) return;
		m_impl->node = MetaYml::Node{};
	}

	Document DocumentAccess::Adopt(MetaYml::Node node)
	{
		Document document;
		document.m_impl->node = std::move(node);
		return document;
	}

	const MetaYml::Node& DocumentAccess::Node(const Document& document)
	{
		return document.m_impl->node;
	}

	MetaYml::Node& DocumentAccess::Node(Document& document)
	{
		return document.m_impl->node;
	}
}
