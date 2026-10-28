#include <reflgen/runtime/registry.h>
#include <cstdlib>
#include <iostream>
#include <new>

import Creator.Core.EngineMode;
#include "reflgen_creator_engine_mode.h"

namespace
{
    thread_local bool tracking = false;
    thread_local std::size_t allocations = 0;
    thread_local std::size_t releases = 0;
}

void* operator new(std::size_t size)
{
    if (void* pointer = std::malloc(size ? size : 1))
    {
        if (tracking) ++allocations;
        return pointer;
    }
    throw std::bad_alloc();
}

void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* pointer) noexcept { if (tracking && pointer) ++releases; std::free(pointer); }
void operator delete[](void* pointer) noexcept { ::operator delete(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { ::operator delete(pointer); }
void operator delete[](void* pointer, std::size_t) noexcept { ::operator delete(pointer); }

int main()
{
    bool valid = true;
    std::size_t registeredAllocations = 0;
    tracking = true;
    {
        reflgen::registry registry;
        reflgen::generated::register_creator_engine_mode(registry);
        const auto* mode = registry.find("EngineMode");
        valid = mode && !mode->is_constructible() && mode->fields().empty() && mode->methods().empty();
        registeredAllocations = allocations;
        for (unsigned i = 0; i < 4096; ++i)
            valid = valid && registry.find("EngineMode") == mode;
        reflgen::generated::register_creator_engine_mode(registry);
        valid = valid && allocations == registeredAllocations;
    }
    tracking = false;
    valid = valid && registeredAllocations != 0 && releases == allocations;
    if (!valid) return 1;
    std::cout << "IFC_ENGINE_MODE_OWNERSHIP_OK lookups=4096 lookupAllocations=0 "
              << "allocations=" << allocations << " releases=" << releases << '\n';
    return 0;
}
