#pragma once
#if defined(CE_CONTRACT_STANDALONE)
#include "../build/ModuleAdapters/Creator.Core.Ownership.h"
#include "../build/ModuleAdapters/Creator.Core.LifetimeToken.h"
#else
import std;
import Creator.Core.Ownership;
import Creator.Core.LifetimeToken;
#endif
struct memory_probe_object
{
    explicit memory_probe_object(int* deaths) : deaths_(deaths) {}
    ~memory_probe_object() { ++*deaths_; }
    int* deaths_;
};
ce::lifetime_token token_from_peer(const own::shared_owner<memory_probe_object>& owner);
void reset_from_peer(own::shared_owner<memory_probe_object>& owner);
