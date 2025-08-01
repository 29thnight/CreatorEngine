#pragma once
#include "BitFlag.h"

struct KeyBitFlag
{
    static constexpr size_t BITS = 256;
    static constexpr size_t CHUNKS = BITS / 32;

    BitFlag chunks[CHUNKS]{};

    void Set(uint8 key) noexcept
    {
        chunks[key / 32].Set(key % 32);
    }

    void Clear(uint8 key) noexcept
    {
        chunks[key / 32].Clear(key % 32);
    }

    bool Test(uint8 key) const noexcept
    {
        return chunks[key / 32].Test(key % 32);
    }

    void Reset()
    {
        for (auto& chunk : chunks)
            chunk = 0;
    }
};

struct MouseBitFlag
{
    flag m_flag = 0; // 32비트 중 3비트만 사용

    void Set(uint8 btn) noexcept
    {
        m_flag |= (1U << static_cast<flag>(btn));
    }

    void Clear(uint8 btn) noexcept
    {
        m_flag &= ~(1U << static_cast<flag>(btn));
    }

    bool Test(uint8 btn) const noexcept
    {
        return (m_flag & (1U << static_cast<flag>(btn))) != 0;
    }

    void Reset() noexcept
    {
        m_flag = 0;
    }
};

struct GamePadBitFlag 
{
    flag m_flag = 0;

    void Set(size_t index) noexcept { m_flag |= (1U << index); }
    void Clear(size_t index) noexcept { m_flag &= ~(1U << index); }
    bool Test(size_t index) const { return (m_flag & (1U << index)) != 0; }
    void Reset() noexcept { m_flag = 0; }
};