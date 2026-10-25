#pragma once
// Generated from Engine/Contracts/Core/Creator.Core.DynamicBitset.ixx; do not edit.
// Regenerate with: py Tools/build/generate-module-adapters.py
#if !defined(CE_CONTRACT_STANDALONE)
#error "Native consumers must import the named module directly."
#endif


#include "../../../Engine/Utility_Framework/Foundation/StandardLibrary.h"

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
            : blocks_(std::move(other.blocks_)), size_(std::exchange(other.size_, 0))
        {
        }

        dynamic_bitset& operator=(dynamic_bitset&& other) noexcept
        {
            if (this != &other)
            {
                blocks_ = std::move(other.blocks_);
                size_ = std::exchange(other.size_, 0);
            }
            return *this;
        }

        explicit dynamic_bitset(std::size_t size, bool value = false) { assign(size, value); }

        std::size_t size() const noexcept { return size_; }
        bool empty() const noexcept { return size_ == 0; }

        bool test(std::size_t index) const
        {
            check(index);
            return blocks_[index / bits_per_block_].test(index % bits_per_block_);
        }

        void set(std::size_t index, bool value = true)
        {
            check(index);
            blocks_[index / bits_per_block_].set(index % bits_per_block_, value);
        }

        void reserve(std::size_t size) { blocks_.reserve(block_count(size)); }

        void assign(std::size_t size, bool value)
        {
            blocks_.assign(block_count(size), block_type(value ? ~std::uint64_t{0} : 0));
            size_ = size;
            trim();
        }

        void resize(std::size_t size, bool value = false)
        {
            const auto previous = size_;
            blocks_.resize(block_count(size), block_type(value ? ~std::uint64_t{0} : 0));
            size_ = size;
            if (value && size > previous && previous % bits_per_block_ != 0)
            {
                blocks_[previous / bits_per_block_] |= block_type(~std::uint64_t{0} << (previous % bits_per_block_));
            }
            trim();
        }

        void push_back(bool value)
        {
            if (size_ == std::numeric_limits<std::size_t>::max())
            {
                throw std::length_error("dynamic_bitset size overflow");
            }
            const auto index = size_;
            resize(index + 1);
            set(index, value);
        }

        void clear() noexcept
        {
            blocks_.clear();
            size_ = 0;
        }

        std::size_t count() const noexcept
        {
            std::size_t result = 0;
            for (const auto& block : std::span{blocks_})
            {
                result += block.count();
            }
            return result;
        }

      private:
        static constexpr std::size_t bits_per_block_ = 64;
        using block_type = std::bitset<bits_per_block_>;

        static std::size_t block_count(std::size_t size) noexcept
        {
            return size / bits_per_block_ + (size % bits_per_block_ != 0);
        }

        void check(std::size_t index) const
        {
            if (index >= size_)
            {
                throw std::out_of_range("dynamic_bitset index");
            }
        }

        void trim() noexcept
        {
            const auto used = size_ % bits_per_block_;
            if (used != 0)
            {
                blocks_.back() &= block_type(~std::uint64_t{0} >> (bits_per_block_ - used));
            }
        }

        std::vector<block_type> blocks_;
        std::size_t size_{};
    };
} // namespace ce
