#pragma once
#if defined(CE_CONTRACT_STANDALONE)
#include "../../../Tools/build/ModuleAdapters/Creator.Core.Ownership.h"
#else
import Creator.Core.Ownership;
#endif
#include "AuthoringDocument.h"

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
