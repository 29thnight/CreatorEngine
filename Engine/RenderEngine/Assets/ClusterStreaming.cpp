#include "ClusterStreaming.h"

namespace assets::cluster
{
    PageIo& PageIo::Shared()
    {
        static PageIo instance;
        return instance;
    }
} // namespace assets::cluster
