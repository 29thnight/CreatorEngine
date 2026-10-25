export module Creator.Core.ContentAbi;

import std;

// Shared identity for content producers, consumers and the native host query.
export namespace ce::content_abi
{
    inline constexpr std::uint32_t version = 1u;
    inline constexpr char token[] = "creator-content-v1";
}
