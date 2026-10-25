export module Creator.Core.BitFlag;

export using flag = unsigned int;

export struct bit_flag
{
  public:
    bit_flag() = default;
    bit_flag(flag value) noexcept : flag_(value) {}

    bit_flag& operator=(flag value) noexcept
    {
        flag_ = value;
        return *this;
    }

    operator flag() const { return flag_; }
    //helper functions
    constexpr void set(flag value) noexcept { flag_ |= 1U << value; }
    constexpr void clear(flag value) noexcept { flag_ &= ~(1U << value); }
    constexpr void toggle(flag value) noexcept { flag_ ^= 1U << value; }
    constexpr bool test(flag value) const noexcept { return flag_ & (1U << value); }

    bit_flag& operator|=(flag value) noexcept
    {
        flag_ |= static_cast<flag>(1U << value);
        return *this;
    }

    bit_flag& operator&=(flag value) noexcept
    {
        flag_ &= static_cast<flag>(1U << value);
        return *this;
    }

    bit_flag& operator^=(flag value) noexcept
    {
        flag_ ^= static_cast<flag>(1U << value);
        return *this;
    }

    [[nodiscard]] bit_flag operator|(flag value) const noexcept
    {
        bit_flag result(*this);
        result |= value;
        return result;
    }

    [[nodiscard]] bit_flag operator&(flag value) const noexcept
    {
        bit_flag result(*this);
        result &= value;
        return result;
    }

    [[nodiscard]] bit_flag operator^(flag value) const noexcept
    {
        bit_flag result(*this);
        result ^= value;
        return result;
    }

  private:
    flag flag_{};
};