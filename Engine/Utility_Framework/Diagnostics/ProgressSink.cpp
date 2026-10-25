#if defined(CE_CONTRACT_STANDALONE)
#include "../../../Tools/build/ModuleAdapters/Creator.Core.Progress.h"
#else
module Creator.Core.Progress;
#endif

namespace ce::progress
{
    sink& get_sink()
    {
        static sink instance;
        return instance;
    }
}
