#if defined(CE_CONTRACT_STANDALONE)
#include "../build/ModuleAdapters/Creator.Core.Paths.h"
#include "../build/ModuleAdapters/Creator.Core.Distribution.h"
#else
import std;
import Creator.Core.Paths;
import Creator.Core.Distribution;
#endif

const void* path_state_from_other_unit()
{
    return &ce::paths::get_state();
}
const void* distribution_from_other_unit()
{
    return &ce::distribution::current_identity();
}
std::filesystem::path assets_from_other_unit()
{
    return ce::paths::relative();
}
