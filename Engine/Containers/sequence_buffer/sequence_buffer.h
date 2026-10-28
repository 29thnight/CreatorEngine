#pragma once
#include "../include/ce/memory.h"
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4324) // intentional payload alignment
#endif
namespace ce
{
    // Monotonic uint64 sequence space: no modular sequence wrap. Out-of-window
    // input is rejected.
    template<class T, size_t Capacity>
    class sequence_buffer
    {
        static_assert(Capacity > 0);
        static_assert(detail::nothrow_move<T>);
        static_assert(noexcept(static_cast<T*>(nullptr)->~T()));

        struct slot
        {
            uint64_t sequence = 0;
            bool live = false;
            alignas(T) unsigned char bytes[sizeof(T)];

            T* value() noexcept
            {
                return std::launder(reinterpret_cast<T*>(bytes));
            }

            const T* value() const noexcept
            {
                return std::launder(reinterpret_cast<const T*>(bytes));
            }
        };

        slot slots_[Capacity];
        uint64_t newest_ = 0;
        bool initialized_ = false;
        size_t size_ = 0;

        void drop(slot& s) noexcept
        {
            if (s.live)
            {
                s.value()->~T();
                s.live = false;
                --size_;
            }
        }

        void advance(uint64_t sequence) noexcept
        {
            if (!initialized_)
            {
                initialized_ = true;
                newest_ = sequence;
                return;
            }
            if (sequence <= newest_)
                return;
            auto delta = sequence - newest_;
            if (delta >= Capacity)
            {
                for (auto& s : slots_)
                    drop(s);
            }
            else
            {
                for (uint64_t step = 1; step <= delta; ++step)
                {
                    auto current = newest_ + step;
                    if (current >= Capacity)
                    {
                        auto expired = current - Capacity;
                        auto& s = slots_[expired % Capacity];
                        if (s.live && s.sequence <= expired)
                            drop(s);
                    }
                }
            }
            newest_ = sequence;
        }

        bool in_window(uint64_t n) const noexcept
        {
            return initialized_ && n <= newest_ && newest_ - n < Capacity;
        }

      public:
        sequence_buffer() = default;
        sequence_buffer(const sequence_buffer&) = delete;
        sequence_buffer& operator=(const sequence_buffer&) = delete;

        ~sequence_buffer()
        {
            clear();
        }

        template<class... A>
        T* emplace(uint64_t sequence, A&&... a)
        {
            if (initialized_ && sequence < newest_ && newest_ - sequence >= Capacity)
                return nullptr;
            T pending(static_cast<A&&>(a)...);
            advance(sequence);
            auto& s = slots_[sequence % Capacity];
            drop(s);
            ::new (static_cast<void*>(s.bytes)) T(static_cast<T&&>(pending));
            s.sequence = sequence;
            s.live = true;
            ++size_;
            return s.value();
        }

        T* find(uint64_t sequence) noexcept
        {
            if (!in_window(sequence))
                return nullptr;
            auto& s = slots_[sequence % Capacity];
            return s.live && s.sequence == sequence ? s.value() : nullptr;
        }

        const T* find(uint64_t sequence) const noexcept
        {
            if (!in_window(sequence))
                return nullptr;
            auto& s = slots_[sequence % Capacity];
            return s.live && s.sequence == sequence ? s.value() : nullptr;
        }

        bool erase(uint64_t sequence) noexcept
        {
            if (!find(sequence))
                return false;
            drop(slots_[sequence % Capacity]);
            return true;
        }

        void clear() noexcept
        {
            for (auto& s : slots_)
                drop(s);
            initialized_ = false;
            newest_ = 0;
        }

        size_t size() const noexcept
        {
            return size_;
        }

        bool empty() const noexcept
        {
            return !size_;
        }
    };
} // namespace ce

#if defined(_MSC_VER)
#pragma warning(pop)
#endif
