#pragma once
#include "../include/ce/all.h"
#include "../ordered-map/ordered_map.h"
#if defined(_MSC_VER)
#define CE_STORAGE_INLINE __forceinline
#else
#define CE_STORAGE_INLINE inline __attribute__((always_inline))
#endif
namespace ce
{
    // Segmented storage: growing the pointer directory never moves live values.
    template<class T, class Allocator = ordered_heap_allocator<T>>
    class segmented_deque
    {
        using traits = std::allocator_traits<Allocator>;
        using directory_allocator = typename traits::template rebind_alloc<T*>;
        using directory_traits = std::allocator_traits<directory_allocator>;
        static constexpr size_t block_size = sizeof(T) <= 16 ? 32 : sizeof(T) <= 64 ? 8 : 1;
        [[no_unique_address]] Allocator allocator_;
        T** directory_ = nullptr;
        size_t capacity_ = 0;
        size_t start_ = block_size / 2;
        size_t size_ = 0;

        T* address(size_t absolute) const noexcept
        {
            return directory_[(absolute / block_size) & (capacity_ - 1)] + absolute % block_size;
        }

        void grow_directory()
        {
            if (capacity_ > size_t(PTRDIFF_MAX) / (2 * sizeof(T*))) throw std::length_error("segmented_deque capacity");
            const size_t count = capacity_ ? capacity_ * 2 : 8;
            directory_allocator allocator(allocator_);
            auto fresh = directory_traits::allocate(allocator, count);
            size_t constructed = 0;
            try
            {
                for (; constructed < count; ++constructed)
                    directory_traits::construct(allocator, fresh + constructed, nullptr);
            }
            catch (...)
            {
                while (constructed) directory_traits::destroy(allocator, fresh + --constructed);
                directory_traits::deallocate(allocator, fresh, count);
                throw;
            }
            if (size_)
            {
                const size_t first = start_ / block_size;
                const size_t blocks = (start_ % block_size + size_ - 1) / block_size + 1;
                for (size_t i = 0; i < blocks; ++i)
                    fresh[(first + i) & (count - 1)] = directory_[(first + i) & (capacity_ - 1)];
            }
            if (directory_)
            {
                for (size_t i = 0; i < capacity_; ++i) directory_traits::destroy(allocator, directory_ + i);
                directory_traits::deallocate(allocator, directory_, capacity_);
            }
            directory_ = fresh;
            capacity_ = count;
        }

        template<class... Args>
        T& construct_at(size_t position, Args&&... args)
        {
            if (size_ == max_size()) throw std::length_error("segmented_deque size");
            const size_t blocks = size_ ? (start_ % block_size + size_ - 1) / block_size + 1 : 0;
            const bool newBlock = !size_ || position / block_size != start_ / block_size &&
                position / block_size != (start_ + size_ - 1) / block_size;
            if (!capacity_ || (newBlock && blocks == capacity_)) grow_directory();
            auto& block = directory_[(position / block_size) & (capacity_ - 1)];
            const bool allocated = !block;
            if (allocated) block = traits::allocate(allocator_, block_size);
            auto target = block + position % block_size;
            try { traits::construct(allocator_, target, std::forward<Args>(args)...); }
            catch (...) { if (allocated) { traits::deallocate(allocator_, block, block_size); block = nullptr; } throw; }
            return *target;
        }

        void release_block(size_t position) noexcept
        {
            auto& block = directory_[(position / block_size) & (capacity_ - 1)];
            traits::deallocate(allocator_, block, block_size);
            block = nullptr;
        }

        void exchange_storage(segmented_deque& other) noexcept
        {
            std::swap(directory_, other.directory_);
            std::swap(capacity_, other.capacity_);
            std::swap(start_, other.start_);
            std::swap(size_, other.size_);
        }

    public:
        using value_type = T;
        using allocator_type = Allocator;
        using size_type = size_t;
        using difference_type = ptrdiff_t;
        using reference = T&;
        using const_reference = const T&;

        template<bool Constant>
        class basic_iterator
        {
            friend class segmented_deque;
            template<bool> friend class basic_iterator;
            using owner_type = std::conditional_t<Constant, const segmented_deque, segmented_deque>;
            owner_type* owner_ = nullptr;
            ptrdiff_t index_ = 0;
            basic_iterator(owner_type* owner, ptrdiff_t index) : owner_(owner), index_(index) {}
        public:
            using value_type = T;
            using difference_type = ptrdiff_t;
            using reference = std::conditional_t<Constant, const T&, T&>;
            using pointer = std::conditional_t<Constant, const T*, T*>;
            using iterator_category = std::random_access_iterator_tag;
            using iterator_concept = std::random_access_iterator_tag;
            basic_iterator() = default;
            template<bool C = Constant> requires C
            basic_iterator(const basic_iterator<false>& other) : owner_(other.owner_), index_(other.index_) {}
            reference operator*() const { return (*owner_)[static_cast<size_t>(index_)]; }
            pointer operator->() const { return std::addressof(**this); }
            reference operator[](difference_type offset) const { return *(*this + offset); }
            basic_iterator& operator++() { ++index_; return *this; }
            basic_iterator operator++(int) { auto old = *this; ++*this; return old; }
            basic_iterator& operator--() { --index_; return *this; }
            basic_iterator operator--(int) { auto old = *this; --*this; return old; }
            basic_iterator& operator+=(difference_type n) { index_ += n; return *this; }
            basic_iterator& operator-=(difference_type n) { index_ -= n; return *this; }
            friend basic_iterator operator+(basic_iterator it, difference_type n) { return it += n; }
            friend basic_iterator operator+(difference_type n, basic_iterator it) { return it += n; }
            friend basic_iterator operator-(basic_iterator it, difference_type n) { return it -= n; }
            friend difference_type operator-(basic_iterator a, basic_iterator b) { return a.index_ - b.index_; }
            friend bool operator==(basic_iterator a, basic_iterator b) { return a.owner_ == b.owner_ && a.index_ == b.index_; }
            friend auto operator<=>(basic_iterator a, basic_iterator b) { return a.index_ <=> b.index_; }
        };
        using iterator = basic_iterator<false>;
        using const_iterator = basic_iterator<true>;
        using reverse_iterator = std::reverse_iterator<iterator>;
        using const_reverse_iterator = std::reverse_iterator<const_iterator>;

        segmented_deque() = default;
        explicit segmented_deque(const Allocator& allocator) : allocator_(allocator) {}
        explicit segmented_deque(size_t count, const Allocator& allocator = Allocator{}) : allocator_(allocator)
        { try { resize(count); } catch (...) { clear(); throw; } }
        segmented_deque(size_t count, const T& value, const Allocator& allocator = Allocator{}) : allocator_(allocator)
        { try { while (size_ < count) emplace_back(value); } catch (...) { clear(); throw; } }
        template<std::input_iterator It>
        segmented_deque(It first, It last, const Allocator& allocator = Allocator{}) : allocator_(allocator)
        { try { for (; first != last; ++first) emplace_back(*first); } catch (...) { clear(); throw; } }
        segmented_deque(std::initializer_list<T> values, const Allocator& allocator = Allocator{})
            : segmented_deque(values.begin(), values.end(), allocator) {}
        segmented_deque(const segmented_deque& other)
            : segmented_deque(other.begin(), other.end(), traits::select_on_container_copy_construction(other.allocator_)) {}
        segmented_deque(const segmented_deque& other, const Allocator& allocator)
            : segmented_deque(other.begin(), other.end(), allocator) {}
        segmented_deque(segmented_deque&& other) noexcept(std::is_nothrow_move_constructible_v<Allocator>)
            : allocator_(std::move(other.allocator_)) { exchange_storage(other); }
        ~segmented_deque() { clear(); }

        segmented_deque& operator=(const segmented_deque& other)
        {
            if (this == &other) return *this;
            if constexpr (traits::propagate_on_container_copy_assignment::value)
            { segmented_deque fresh(other, other.allocator_); clear(); allocator_ = other.allocator_; exchange_storage(fresh); }
            else { segmented_deque fresh(other, allocator_); exchange_storage(fresh); }
            return *this;
        }
        segmented_deque& operator=(segmented_deque&& other)
            noexcept(traits::propagate_on_container_move_assignment::value && std::is_nothrow_move_assignable_v<Allocator>)
        {
            if (this == &other) return *this;
            if constexpr (traits::propagate_on_container_move_assignment::value)
            { clear(); allocator_ = std::move(other.allocator_); exchange_storage(other); }
            else if (allocator_ == other.allocator_) { clear(); exchange_storage(other); }
            else
            {
                segmented_deque fresh(allocator_);
                for (auto& value : other) fresh.emplace_back(std::move(value));
                exchange_storage(fresh);
                other.clear();
            }
            return *this;
        }
        segmented_deque& operator=(std::initializer_list<T> values)
        { assign(values.begin(), values.end()); return *this; }
        void swap(segmented_deque& other)
        {
            if constexpr (traits::propagate_on_container_swap::value) std::swap(allocator_, other.allocator_);
            else if (!(allocator_ == other.allocator_)) throw std::invalid_argument("segmented_deque allocator mismatch");
            exchange_storage(other);
        }
        friend void swap(segmented_deque& a, segmented_deque& b) { a.swap(b); }
        Allocator get_allocator() const { return allocator_; }
        size_t size() const noexcept { return size_; }
        bool empty() const noexcept { return !size_; }
        size_t max_size() const noexcept { return std::min(size_t(PTRDIFF_MAX), traits::max_size(allocator_)); }
        T& operator[](size_t index) noexcept { return *address(start_ + index); }
        const T& operator[](size_t index) const noexcept { return *address(start_ + index); }
        T& at(size_t index) { if (index >= size_) throw std::out_of_range("segmented_deque"); return (*this)[index]; }
        const T& at(size_t index) const { if (index >= size_) throw std::out_of_range("segmented_deque"); return (*this)[index]; }
        T& front() noexcept { return (*this)[0]; }
        const T& front() const noexcept { return (*this)[0]; }
        T& back() noexcept { return (*this)[size_ - 1]; }
        const T& back() const noexcept { return (*this)[size_ - 1]; }
        iterator begin() noexcept { return {this, 0}; }
        iterator end() noexcept { return {this, static_cast<ptrdiff_t>(size_)}; }
        const_iterator begin() const noexcept { return {this, 0}; }
        const_iterator end() const noexcept { return {this, static_cast<ptrdiff_t>(size_)}; }
        const_iterator cbegin() const noexcept { return begin(); }
        const_iterator cend() const noexcept { return end(); }
        reverse_iterator rbegin() noexcept { return reverse_iterator(end()); }
        reverse_iterator rend() noexcept { return reverse_iterator(begin()); }
        const_reverse_iterator rbegin() const noexcept { return const_reverse_iterator(end()); }
        const_reverse_iterator rend() const noexcept { return const_reverse_iterator(begin()); }
        template<class... Args> T& emplace_back(Args&&... args)
        { auto& result = construct_at(start_ + size_, std::forward<Args>(args)...); ++size_; return result; }
        template<class... Args> T& emplace_front(Args&&... args)
        { auto& result = construct_at(start_ - 1, std::forward<Args>(args)...); --start_; ++size_; return result; }
        void push_back(const T& value) { emplace_back(value); }
        void push_back(T&& value) { emplace_back(std::move(value)); }
        void push_front(const T& value) { emplace_front(value); }
        void push_front(T&& value) { emplace_front(std::move(value)); }
        void pop_back() noexcept
        {
            const auto position = start_ + size_ - 1;
            traits::destroy(allocator_, address(position));
            --size_;
            if (!size_ || position / block_size != (start_ + size_ - 1) / block_size) release_block(position);
            if (!size_) start_ = block_size / 2;
        }
        void pop_front() noexcept
        {
            const auto position = start_;
            traits::destroy(allocator_, address(position));
            ++start_; --size_;
            if (!size_ || position / block_size != start_ / block_size) release_block(position);
            if (!size_) start_ = block_size / 2;
        }
        void clear() noexcept
        {
            while (size_) pop_back();
            if (directory_)
            {
                directory_allocator allocator(allocator_);
                for (size_t i = 0; i < capacity_; ++i) directory_traits::destroy(allocator, directory_ + i);
                directory_traits::deallocate(allocator, directory_, capacity_);
                directory_ = nullptr; capacity_ = 0;
            }
        }
        void resize(size_t count) { while (size_ > count) pop_back(); while (size_ < count) emplace_back(); }
        void resize(size_t count, const T& value) { while (size_ > count) pop_back(); while (size_ < count) emplace_back(value); }
        template<std::input_iterator It> void assign(It first, It last)
        { segmented_deque fresh(first, last, allocator_); exchange_storage(fresh); }
        iterator erase(const_iterator position) { return erase(position, position + 1); }
        iterator erase(const_iterator first, const_iterator last)
        {
            const auto offset = first - cbegin();
            const auto removed = last - first;
            if (!removed) return begin() + offset;
            if (offset < cend() - last)
            {
                std::move_backward(begin(), begin() + offset, begin() + offset + removed);
                for (ptrdiff_t i = 0; i < removed; ++i) pop_front();
            }
            else
            {
                std::move(begin() + offset + removed, end(), begin() + offset);
                for (ptrdiff_t i = 0; i < removed; ++i) pop_back();
            }
            return begin() + offset;
        }
        friend bool operator==(const segmented_deque& a, const segmented_deque& b)
        { return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin()); }
    };

    template<class T, class Allocator, class Predicate>
    size_t erase_if(segmented_deque<T, Allocator>& values, Predicate predicate)
    {
        const auto before = values.size();
        values.erase(std::remove_if(values.begin(), values.end(), predicate), values.end());
        return before - values.size();
    }


}
#undef CE_STORAGE_INLINE
