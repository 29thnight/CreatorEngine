#pragma once
#include "BaseTypeDef.h"

struct BitFlag
{
public:
    BitFlag() = default;
    BitFlag(flag _flag) noexcept
        : m_flag(_flag)
    {
    }

    BitFlag& operator= (flag _flag) noexcept
    {
        m_flag = _flag;
        return *this;
    }

    operator flag() const
    {
        return m_flag;
    }
    //helper functions
    constexpr void Set(flag _flag) noexcept
    {
        m_flag |= 1U << _flag;
    }
    constexpr void Clear(flag _flag) noexcept
    {
        m_flag &= ~(1U << _flag);
    }
    constexpr void Toggle(flag _flag) noexcept
    {
        m_flag ^= 1U << _flag;
    }
    constexpr bool Test(flag _flag) const noexcept
    {
        return m_flag & (1U << _flag);
    }

    BitFlag& operator|= (flag _flag) noexcept
    {
        m_flag |= static_cast<flag>(1U << _flag);
        return *this;
    }

    BitFlag& operator&= (flag _flag) noexcept
    {
        m_flag &= static_cast<flag>(1U << _flag);
        return *this;
    }

    BitFlag& operator^= (flag _flag) noexcept
    {
        m_flag ^= static_cast<flag>(1U << _flag);
        return *this;
    }

    [[nodiscard]] BitFlag operator|(flag _flag) const noexcept
    {
        BitFlag result(*this);
        result |= _flag;
        return result;
    }

    [[nodiscard]] BitFlag operator&(flag _flag) const noexcept
    {
        BitFlag result(*this);
        result &= _flag;
        return result;
    }

    [[nodiscard]] BitFlag operator^(flag _flag) const noexcept
    {
        BitFlag result(*this);
        result ^= _flag;
        return result;
    }

private:
    flag m_flag{};
};