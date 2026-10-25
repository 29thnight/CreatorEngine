#if defined(CE_CONTRACT_STANDALONE)
#include "../build/ModuleAdapters/Creator.Core.Ownership.h"
#include "../build/ModuleAdapters/Creator.Core.LifetimeToken.h"
#else
import Creator.Core.Ownership;
import Creator.Core.LifetimeToken;
#endif
#include "memory_ownership_probe_types.h"
ce::lifetime_token token_from_peer(const own::shared_owner<memory_probe_object>& owner)
{
    return ce::lifetime_token(owner);
}
void reset_from_peer(own::shared_owner<memory_probe_object>& owner)
{
    owner.reset();
}
