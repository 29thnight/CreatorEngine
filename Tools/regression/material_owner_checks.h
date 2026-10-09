#pragma once

#include "../../Engine/Utility_Framework/Ownership.h"
#include <memory>

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
