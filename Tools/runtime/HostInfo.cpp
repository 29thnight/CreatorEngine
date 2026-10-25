#if defined(CE_CONTRACT_STANDALONE)
#include "../build/ModuleAdapters/Creator.Core.Distribution.h"
#else
import Creator.Core.Distribution;
#endif
#include <yvals.h> // Read MSVC's actual iterator ABI setting; modules do not export macros.
#include "HostAbi.h"
#include "../../Engine/Utility_Framework/Runtime/ScriptApiVersion.h"
#if defined(CE_CONTRACT_STANDALONE)
#include "../build/ModuleAdapters/Creator.Core.ContentAbi.h"
#else
import Creator.Core.ContentAbi;
#endif
#include "../../Engine/Utility_Framework/Foundation/StandardLibrary.h"

extern "C" __declspec(dllexport) const CreatorHostInfoV1* __cdecl CreatorHostGetInfoV1()
{
    (void)ce::distribution::current_identity();
    static constexpr CreatorHostInfoV1 info{
        sizeof(CreatorHostInfoV1), 1, _MSC_FULL_VER, _ITERATOR_DEBUG_LEVEL,
#ifdef _DEBUG
        1,
#else
        0,
#endif
        CE_SHIPPING, CreatorScriptApiVersion, sizeof(void*) * 8,
        ce::distribution::local_development ? 1u : 0u,
        ce::distribution::product_name, ce::distribution::feature_release, ce::distribution::build
    };
    return &info;
}

extern "C" __declspec(dllexport) const CreatorHostContentInfoV1* __cdecl CreatorHostGetContentInfoV1()
{
    static constexpr CreatorHostContentInfoV1 info{
        sizeof(CreatorHostContentInfoV1), ce::content_abi::version, ce::content_abi::token
    };
    return &info;
}
