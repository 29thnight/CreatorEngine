#pragma once
#include "AuthoringDocument.h"
#include "Ownership.h"

namespace Authoring
{
    class WriteDocument;

    // Internal ownership storage. An empty document does not reference a parser
    // or writer destructor. Access installs backend operations only on admission.
    struct Document::Impl
    {
        own::shared_owner<WriteDocument> document;
        bool (*isEmpty)(const WriteDocument&) noexcept = nullptr;
    };
}
