#pragma once
// Generated from Engine/Contracts/Core/Creator.Core.ContentAbi.ixx; do not edit.
// Regenerate with: py Tools/build/generate-module-adapters.py
#if !defined(CE_CONTRACT_STANDALONE)
#error "Native consumers must import the named module directly."
#endif


#include "../../../Engine/Utility_Framework/Foundation/StandardLibrary.h"

// Shared identity for content producers, consumers and the native host query.
namespace ce::content_abi
{
    inline constexpr std::uint32_t version = 1u;
    inline constexpr char token[] = "creator-content-v1";
}
