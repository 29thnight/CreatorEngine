module;
#include <CreatorContractConfig.h>
// stderr is a C macro and is intentionally not exported by import std.
#include <stdio.h>

export module Creator.Contracts;

import std;

#include "ContractImplementation.h"

export namespace ce::contracts
{
    using ::ce::contract_detail::assertions_enabled;
    using ::ce::contract_detail::assert_that;
    using ::ce::contract_detail::enforce_that;
}
