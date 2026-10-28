#pragma once

#include <cstdint>

namespace Runtime
{
    struct TickStatus
    {
        std::uint64_t epoch{}, committed{}, executing{};
        std::uint32_t queued{}, active{};
    };
    struct TickSnapshotHeader
    {
        std::uint64_t epoch{}, tick{};
        std::uint32_t applied{}, rejected{}, bytes{}, schema{};
    };
}
