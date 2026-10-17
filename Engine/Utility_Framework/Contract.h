#pragma once

// Source probes and reflgen's Clang parser explicitly select the adapter. Native
// compilation imports the module. The adapter uses the same implementation text
// without requiring MSVC module artifacts in Clang or standalone source probes.
#if defined(CE_CONTRACT_STANDALONE)
#include <CreatorContractConfig.h>
#include <cstdio>
#include <cstdlib>
#include <source_location>
#include "../Contracts/ContractImplementation.h"
namespace ce::contracts
{
    using ::ce::contract_detail::assertions_enabled;
    using ::ce::contract_detail::assert_that;
    using ::ce::contract_detail::enforce_that;
}
#else
import Creator.Contracts;
#endif
