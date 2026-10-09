#pragma once
#include <cstdint>

// Shared by the content producer, consumer, and compiled host identity query.
// Change this identity when cooked content can no longer be consumed unchanged.
namespace CreatorContentAbi
{
    inline constexpr std::uint32_t Version = 1u;
    inline constexpr char Token[] = "creator-content-v1";
}
