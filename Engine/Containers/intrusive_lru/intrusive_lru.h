#pragma once
#include "../include/ce/memory.h"
namespace ce
{
    template<class Index = uint32_t>
    struct lru_link
    {
        Index previous = Index(-1), next = Index(-1);
        bool linked = false;
    };

    template<class Index>
    struct array_lru_links
    {
        lru_link<Index>* data = nullptr;
        array_lru_links() = default;

        explicit array_lru_links(lru_link<Index>* p) : data(p)
        {
        }

        lru_link<Index>& operator()(Index i) const noexcept
        {
            return data[i];
        }
    };

    // The caller owns links; an accessor can expose a link embedded inside each
    // real resource. One link may belong to one list only. No allocations, no
    // synchronization.
    template<class Index = uint32_t, class Accessor = array_lru_links<Index>>
    class intrusive_lru
    {
        Accessor links_;
        size_t capacity_, size_ = 0;
        Index first_ = Index(-1), last_ = Index(-1);

        void check_index(Index i) const
        {
            if (size_t(i) >= capacity_)
                throw bounds_error{};
        }

      public:
        static constexpr Index invalid = Index(-1);

        intrusive_lru(lru_link<Index>* links, size_t capacity) : links_(links), capacity_(capacity)
        {
            if (capacity > size_t(invalid))
                throw bounds_error{};
        }

        intrusive_lru(Accessor links, size_t capacity) : links_(links), capacity_(capacity)
        {
            if (capacity > size_t(invalid))
                throw bounds_error{};
        }

        intrusive_lru(const intrusive_lru&) = delete;
        intrusive_lru& operator=(const intrusive_lru&) = delete;

        ~intrusive_lru()
        {
            clear();
        }

        bool contains(Index i) const noexcept
        {
            return size_t(i) < capacity_ && links_(i).linked;
        }

        bool push_front(Index i)
        {
            check_index(i);
            if (links_(i).linked)
                return false;
            auto& n = links_(i);
            n.previous = invalid;
            n.next = first_;
            n.linked = true;
            if (first_ != invalid)
                links_(first_).previous = i;
            else
                last_ = i;
            first_ = i;
            ++size_;
            return true;
        }

        bool erase(Index i) noexcept
        {
            if (!contains(i))
                return false;
            auto& n = links_(i);
            if (n.previous != invalid)
                links_(n.previous).next = n.next;
            else
                first_ = n.next;
            if (n.next != invalid)
                links_(n.next).previous = n.previous;
            else
                last_ = n.previous;
            n.previous = n.next = invalid;
            n.linked = false;
            --size_;
            return true;
        }

        bool touch(Index i)
        {
            if (!contains(i))
                return false;
            if (first_ == i)
                return true;
            erase(i);
            push_front(i);
            return true;
        }

        Index pop_back() noexcept
        {
            auto i = last_;
            if (i != invalid)
                erase(i);
            return i;
        }

        Index front() const noexcept
        {
            return first_;
        }

        Index back() const noexcept
        {
            return last_;
        }

        size_t size() const noexcept
        {
            return size_;
        }

        bool empty() const noexcept
        {
            return !size_;
        }

        void clear() noexcept
        {
            while (last_ != invalid)
                erase(last_);
        }

        template<class F>
        void for_each(F&& f) const
        {
            for (auto i = first_; i != invalid; i = links_(i).next)
                f(i);
        }
    };
} // namespace ce
