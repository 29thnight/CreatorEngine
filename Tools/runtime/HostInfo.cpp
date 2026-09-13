#include "HostAbi.h"
#include "../../Engine/Utility_Framework/ScriptApiVersion.h"
#include "../../Engine/Utility_Framework/EngineVersion.h"
#include "../../Engine/Utility_Framework/EngineDistributionIdentity.h"
#include <vector>

extern "C" __declspec(dllexport) const CreatorHostInfoV1* __cdecl CreatorHostGetInfoV1()
{
    (void)CurrentEngineDistributionIdentity();
    static constexpr CreatorHostInfoV1 info{
        sizeof(CreatorHostInfoV1), 1, _MSC_FULL_VER, _ITERATOR_DEBUG_LEVEL,
#ifdef _DEBUG
        1,
#else
        0,
#endif
        CE_SHIPPING, CreatorScriptApiVersion, sizeof(void*) * 8,
        CreatorEngineVersion::LocalDevelopment ? 1u : 0u,
        CreatorEngineVersion::ProductName, CreatorEngineVersion::FeatureRelease, CreatorEngineVersion::Build
    };
    return &info;
}
