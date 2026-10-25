#pragma once
#if defined(CE_CONTRACT_STANDALONE)
#include "../build/ModuleAdapters/Creator.Core.Ownership.h"
#else
import Creator.Core.Ownership;
#endif

#include "../../Engine/Utility_Framework/Foundation/StandardLibrary.h"

namespace material_graph_test
{
    // Both parameters retain their objects throughout the address comparison.
    // ownership_cpp deliberately provides only null comparisons on owner types.
    template<class T, class U>
    bool SamePinnedObject(const own::shared_owner<T>& left, const own::shared_owner<U>& right)
    {
        return left && right ? std::addressof(*left) == std::addressof(*right) : !left && !right;
    }

    template<class T, class U>
    bool SamePinnedObject(const own::local_view<T>& left, const own::shared_owner<U>& right)
    {
        return left && right ? std::addressof(*left) == std::addressof(*right) : !left && !right;
    }

    template<class T, class U>
    bool SamePinnedObject(const own::shared_owner<T>& left, const own::local_view<U>& right)
    {
        return SamePinnedObject(right, left);
    }

    template<class T, class U>
    bool SamePinnedObject(const own::local_view<T>& left, const own::local_view<U>& right)
    {
        return left && right ? std::addressof(*left) == std::addressof(*right) : !left && !right;
    }
}
