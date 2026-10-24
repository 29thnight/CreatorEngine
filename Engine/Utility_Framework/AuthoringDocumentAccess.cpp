#include "AuthoringDocumentAccess.h"
#include "AuthoringDocumentStorage.h"

namespace Authoring
{
	Document DocumentAccess::Adopt(WriteDocument writer)
	{
		Document document;
		document.m_impl->document =
			own::make_shared<WriteDocument>(std::move(writer));
        document.m_impl->isEmpty = [](const WriteDocument& value) noexcept { return value.IsEmpty(); };
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
        {
            document.m_impl->document = own::make_shared<WriteDocument>();
            document.m_impl->isEmpty = [](const WriteDocument& value) noexcept { return value.IsEmpty(); };
        }
		return document.m_impl->document->Root();
	}
}
