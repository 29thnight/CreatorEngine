#pragma once
#include "StandardLibrary.h"

namespace ce
{
    // Runtime-sized bit storage. Reads return bool values; writes never create
    // container-bound iterator proxies. This class provides no synchronization.
    class dynamic_bitset
    {
    public:
        dynamic_bitset() = default;
        dynamic_bitset(const dynamic_bitset&) = default;
        dynamic_bitset& operator=(const dynamic_bitset&) = default;

        dynamic_bitset(dynamic_bitset&& other) noexcept
            : m_blocks(std::move(other.m_blocks)), m_size(std::exchange(other.m_size, 0))
        {
        }

        dynamic_bitset& operator=(dynamic_bitset&& other) noexcept
        {
            if (this != &other)
            {
                m_blocks = std::move(other.m_blocks);
                m_size = std::exchange(other.m_size, 0);
            }
            return *this;
        }

        explicit dynamic_bitset(std::size_t size, bool value = false)
        {
            assign(size, value);
        }

        std::size_t size() const noexcept { return m_size; }
        bool empty() const noexcept { return m_size == 0; }

        bool test(std::size_t index) const
        {
            check(index);
            return m_blocks[index / kBits].test(index % kBits);
        }

        void set(std::size_t index, bool value = true)
        {
            check(index);
            m_blocks[index / kBits].set(index % kBits, value);
        }

        void reserve(std::size_t size)
        {
            m_blocks.reserve(block_count(size));
        }

        void assign(std::size_t size, bool value)
        {
            m_blocks.assign(block_count(size), block_type(value ? ~std::uint64_t{0} : 0));
            m_size = size;
            trim();
        }

        void resize(std::size_t size, bool value = false)
        {
            const auto previous = m_size;
            m_blocks.resize(block_count(size), block_type(value ? ~std::uint64_t{0} : 0));
            m_size = size;
            if (value && size > previous && previous % kBits != 0)
            {
                m_blocks[previous / kBits] |= block_type(~std::uint64_t{0} << (previous % kBits));
            }
            trim();
        }

        void push_back(bool value)
        {
            if (m_size == std::numeric_limits<std::size_t>::max())
            {
                throw std::length_error("dynamic_bitset size overflow");
            }
            const auto index = m_size;
            resize(index + 1);
            set(index, value);
        }

        void clear() noexcept
        {
            m_blocks.clear();
            m_size = 0;
        }

        std::size_t count() const noexcept
        {
            std::size_t result = 0;
            for (const auto& block : std::span{m_blocks})
            {
                result += block.count();
            }
            return result;
        }

    private:
        static constexpr std::size_t kBits = 64;
        using block_type = std::bitset<kBits>;

        static std::size_t block_count(std::size_t size) noexcept
        {
            return size / kBits + (size % kBits != 0);
        }

        void check(std::size_t index) const
        {
            if (index >= m_size)
            {
                throw std::out_of_range("dynamic_bitset index");
            }
        }

        void trim() noexcept
        {
            const auto used = m_size % kBits;
            if (used != 0)
            {
                m_blocks.back() &= block_type(~std::uint64_t{0} >> (kBits - used));
            }
        }

        std::vector<block_type> m_blocks;
        std::size_t m_size{};
    };
}
