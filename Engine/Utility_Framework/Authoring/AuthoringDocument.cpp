#include "AuthoringDocumentStorage.h"

namespace Authoring
{
    Document::Document() : m_impl(std::make_unique<Impl>()) {}
    Document::~Document() = default;
    Document::Document(Document&&) noexcept = default;
    Document& Document::operator=(Document&&) noexcept = default;

    bool Document::IsEmpty() const noexcept
    {
        return !m_impl || !m_impl->document || !m_impl->isEmpty
            || m_impl->isEmpty(*m_impl->document);
    }

    void Document::Clear() noexcept
    {
        if (!m_impl) return;
        m_impl->document.reset();
        m_impl->isEmpty = nullptr;
    }
}
