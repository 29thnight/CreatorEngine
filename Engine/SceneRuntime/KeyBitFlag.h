#pragma once
#include "../Utility_Framework/Foundation/BaseTypeDef.h"
#include "../Utility_Framework/Foundation/StandardLibrary.h"
#if defined(CE_CONTRACT_STANDALONE)
#include "../../Tools/build/ModuleAdapters/Creator.Core.BitFlag.h"
#else
import Creator.Core.BitFlag;
#endif

struct key_bit_flag
{
    static constexpr size_t bit_count = 256;
    static constexpr size_t chunk_count = bit_count / 32;

    bit_flag chunks_[chunk_count]{};

    void set(uint8 key) noexcept { chunks_[key / 32].set(key % 32); }

    void clear(uint8 key) noexcept { chunks_[key / 32].clear(key % 32); }

    bool test(uint8 key) const noexcept { return chunks_[key / 32].test(key % 32); }

    void reset()
    {
        for (auto& chunk : chunks_)
        {
            chunk = 0;
        }
    }
};

struct mouse_bit_flag
{
    flag flag_ = 0; // 32비트 중 3비트만 사용

    void set(uint8 btn) noexcept { flag_ |= (1U << static_cast<flag>(btn)); }

    void clear(uint8 btn) noexcept { flag_ &= ~(1U << static_cast<flag>(btn)); }

    bool test(uint8 btn) const noexcept { return (flag_ & (1U << static_cast<flag>(btn))) != 0; }

    void reset() noexcept { flag_ = 0; }
};

struct gamepad_bit_flag
{
    flag flag_ = 0;

    void set(size_t index) noexcept { flag_ |= (1U << index); }
    void clear(size_t index) noexcept { flag_ &= ~(1U << index); }
    bool test(size_t index) const { return (flag_ & (1U << index)) != 0; }
    void reset() noexcept { flag_ = 0; }
};