#pragma once
#include "../Utility_Framework/StandardLibrary.h"

namespace material_graph::detail
{
// Owned by one immutable program preparation. Pending polls must not spend the
// frame's admission budget again, otherwise a slow head request starves its tail.
class NativeAdmission
{
public:
    bool CanPoll(const void* request, unsigned& remaining, bool nonblocking)
    {
        if (nonblocking && admitted_.contains(request)) return true;
        if (!remaining) return false;
        --remaining;
        if (nonblocking) admitted_.insert(request);
        return true;
    }
private:
    std::unordered_set<const void*> admitted_;
};
}
