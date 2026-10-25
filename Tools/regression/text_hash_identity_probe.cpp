#if defined(CE_CONTRACT_STANDALONE)
#include "../build/ModuleAdapters/Creator.Core.Sha256.h"
#include "../build/ModuleAdapters/Creator.Core.Strings.h"
#else
import std;
import Creator.Core.Sha256;
import Creator.Core.Strings;
#endif

ce::hash::sha256_digest text_hash_identity(std::string_view text)
{
    return ce::hash::sha256::compute(text.data(), text.size());
}
std::string text_lower_identity(std::string_view text)
{
    return ce::strings::to_lower(text);
}
