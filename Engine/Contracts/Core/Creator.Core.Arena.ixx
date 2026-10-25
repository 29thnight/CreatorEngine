export module Creator.Core.Arena;

import std;
import Creator.Core.HeapAllocator;

export namespace ce
{
    struct arena_stats
    {
        std::size_t retained_bytes{};
        std::size_t block_count{};
        std::size_t active_blocks{};
        std::size_t active_capacity{};
        std::size_t used_bytes{};
        std::uint64_t resets{};
    };

    // One writer per resource. Owners destroy every object/container before reset.
    // Raw blocks stay at stable addresses until this resource is destroyed.
    class arena_resource final
    {
      public:
        arena_resource() = default;
        ~arena_resource();
        arena_resource(const arena_resource&) = delete;
        arena_resource& operator=(const arena_resource&) = delete;
        [[nodiscard]] void* allocate(std::size_t bytes, std::size_t alignment);
        bool try_allocate(std::size_t bytes, std::size_t alignment, void*& memory) noexcept
        {
            if (!current_ || !current_->used || !bytes || !alignment || (alignment & (alignment - 1)))
            {
                return false;
            }
            auto* begin = current_->data() + current_->used;
            const std::size_t padding = (std::uintptr_t{} - reinterpret_cast<std::uintptr_t>(begin)) & (alignment - 1);
            const std::size_t available = current_->capacity - current_->used;
            if (padding > available || bytes > available - padding)
            {
                return false;
            }
            memory = begin + padding;
            current_->used += padding + bytes;
            stats_.used_bytes += bytes;
            return true;
        }
        void reset() noexcept;
        const arena_stats& stats() const noexcept { return stats_; }

      private:
        struct block
        {
            block* next{};
            std::size_t capacity{};
            std::size_t used{};
            std::byte* data() noexcept { return reinterpret_cast<std::byte*>(this + 1); }
        };
        block* head_{};
        block* current_{};
        arena_stats stats_;
    };

    // Single-writer objects with stable addresses and reverse destruction order.
    // Reset destroys captures before rewinding the retained non-pmr blocks.
    class arena_objects final
    {
      public:
        ~arena_objects() { reset(); }
        arena_objects() = default;
        arena_objects(const arena_objects&) = delete;
        arena_objects& operator=(const arena_objects&) = delete;

        template<class T, class... Args>
        T* emplace(Args&&... args)
        {
            static_assert(std::is_nothrow_destructible_v<T>);
            if constexpr (std::is_trivially_destructible_v<T>)
            {
                auto* memory = static_cast<T*>(resource_.allocate(sizeof(T), alignof(T)));
                return std::construct_at(memory, std::forward<Args>(args)...);
            }
            else
            {
                auto* record = static_cast<destructor_record*>(
                    resource_.allocate(sizeof(destructor_record), alignof(destructor_record)));
                auto* memory = static_cast<T*>(resource_.allocate(sizeof(T), alignof(T)));
                // A throwing constructor never publishes a destructor record.
                auto* value = std::construct_at(memory, std::forward<Args>(args)...);
                std::construct_at(record, destructor_record{head_, value, [](void* object) noexcept
                                                            { std::destroy_at(static_cast<T*>(object)); }});
                head_ = record;
                return value;
            }
        }

        template<class T>
        std::span<const T> copy(std::span<const T> values)
        {
            if (values.empty())
            {
                return {};
            }
            if (values.size() > (std::numeric_limits<std::size_t>::max)() / sizeof(T))
            {
                throw std::bad_array_new_length();
            }
            auto* data = static_cast<T*>(resource_.allocate(values.size_bytes(), alignof(T)));
            // 단순 값은 파괴할 소유자가 없다. 원소별 생성/파괴 목록 없이
            // 한 번 복사하고, 주소는 이 arena를 reset할 때까지 유지한다.
            if constexpr (std::is_trivially_copyable_v<T> && std::is_trivially_destructible_v<T>)
            {
                std::memcpy(data, values.data(), values.size_bytes());
                return {data, values.size()};
            }
            else
            {
                struct array_owner
                {
                    T* data;
                    std::size_t size{};
                    ~array_owner()
                    {
                        while (size)
                        {
                            std::destroy_at(data + --size);
                        }
                    }
                };
                auto* owner = emplace<array_owner>(data);
                try
                {
                    for (const auto& value : values)
                    {
                        std::construct_at(data + owner->size, value);
                        ++owner->size;
                    }
                }
                catch (...)
                {
                    while (owner->size)
                    {
                        std::destroy_at(data + --owner->size);
                    }
                    throw;
                }
                return {data, owner->size};
            }
        }

        void reset() noexcept
        {
            while (head_)
            {
                auto* record = head_;
                head_ = record->previous;
                record->destroy(record->value);
            }
            resource_.reset();
        }
        arena_stats stats() const noexcept { return resource_.stats(); }

      private:
        struct destructor_record
        {
            destructor_record* previous;
            void* value;
            void (*destroy)(void*) noexcept;
        };
        arena_resource resource_;
        destructor_record* head_{};
    };

    // Trivial values use raw iterators and retained arena blocks.
    // The writer must finish before its resource is reset; no resource ownership
    // is transferred by a span returned from this buffer.
    template<class T>
    class arena_buffer final
    {
        static_assert(std::is_trivially_copyable_v<T> && std::is_trivially_destructible_v<T>);

      public:
        arena_buffer() = default;
        explicit arena_buffer(arena_resource& resource) noexcept : resource_(&resource) {}
        arena_buffer(const arena_buffer&) = delete;
        arena_buffer& operator=(const arena_buffer&) = delete;
        arena_buffer(arena_buffer&&) noexcept = default;
        arena_buffer& operator=(arena_buffer&&) noexcept = default;
        T* data() noexcept { return data_; }
        const T* data() const noexcept { return data_; }
        std::size_t size() const noexcept { return size_; }
        std::size_t capacity() const noexcept { return capacity_; }
        std::size_t max_size() const noexcept { return (std::numeric_limits<std::size_t>::max)() / sizeof(T); }
        bool empty() const noexcept { return !size_; }
        T* begin() noexcept { return data_; }
        const T* begin() const noexcept { return data_; }
        T* end() noexcept { return size_ ? data_ + size_ : data_; }
        const T* end() const noexcept { return size_ ? data_ + size_ : data_; }
        T& operator[](std::size_t index) noexcept { return data_[index]; }
        const T& operator[](std::size_t index) const noexcept { return data_[index]; }
        operator std::span<const T>() const noexcept { return {data_, size_}; }
        void clear() noexcept { size_ = 0; }
        void pop_back() noexcept
        {
            if (size_)
            {
                --size_;
            }
        }
        void assign(std::initializer_list<T> values)
        {
            clear();
            reserve(values.size());
            for (const auto& value : values)
            {
                push_back(value);
            }
        }
        void assign(std::size_t count, const T& value)
        {
            const T copy = value;
            reserve(count);
            std::fill_n(data_, count, copy);
            size_ = count;
        }
        template<class Predicate>
        void erase_if(const Predicate& predicate)
        {
            std::size_t destination = 0;
            for (std::size_t index = 0; index < size_; ++index)
            {
                if (!predicate(data_[index]))
                {
                    data_[destination++] = data_[index];
                }
            }
            size_ = destination;
        }
        void reserve(std::size_t capacity)
        {
            if (capacity <= capacity_)
            {
                return;
            }
            if (!resource_ || capacity > (std::numeric_limits<std::size_t>::max)() / sizeof(T))
            {
                throw std::bad_array_new_length();
            }
            auto* data = static_cast<T*>(resource_->allocate(capacity * sizeof(T), alignof(T)));
            if (size_)
            {
                std::memcpy(data, data_, size_ * sizeof(T));
            }
            data_ = data;
            capacity_ = capacity;
        }
        void resize(std::size_t size)
        {
            reserve(size);
            if (size > size_)
            {
                std::fill_n(data_ + size_, size - size_, T{});
            }
            size_ = size;
        }
        void push_back(const T& value)
        {
            // A caller may append an element from this same buffer.
            const T copy = value;
            if (size_ == capacity_)
            {
                const auto maximum = (std::numeric_limits<std::size_t>::max)() / sizeof(T);
                if (capacity_ > maximum / 2)
                {
                    throw std::bad_array_new_length();
                }
                reserve(capacity_ ? capacity_ * 2 : 16);
            }
            data_[size_++] = copy;
        }
        template<class Iterator>
        void insert(T* position, Iterator first, Iterator last)
        {
            if (position != end())
            {
                throw std::invalid_argument("Arena buffer only appends ranges.");
            }
            for (; first != last; ++first)
            {
                push_back(*first);
            }
        }

      private:
        arena_resource* resource_{};
        T* data_{};
        std::size_t size_{}, capacity_{};
    };

    // Stateful STL allocator without a virtual memory_resource dispatch. Nested
    // containers and map values receive the same writer domain through construct.
    template<typename T>
    class arena_allocator
    {
      public:
        using value_type = T;
        using is_always_equal = std::false_type;

        arena_allocator() noexcept = default;
        arena_allocator(arena_resource* resource) noexcept : resource_(resource) {}
        template<typename U>
        arena_allocator(const arena_allocator<U>& other) noexcept : resource_(other.resource())
        {
        }

        arena_resource* resource() const noexcept { return resource_; }
        // A plain container copy may outlive the source arena. Only explicitly
        // allocator-bound construction is allowed to borrow the source domain.
        arena_allocator select_on_container_copy_construction() const noexcept { return {}; }

        [[nodiscard]] T* allocate(std::size_t count)
        {
            if (count > (std::numeric_limits<std::size_t>::max)() / sizeof(T))
            {
                throw std::bad_array_new_length();
            }
            return resource_ ? static_cast<T*>(resource_->allocate(count * sizeof(T), alignof(T)))
                             : heap_allocator<T>{}.allocate(count);
        }

        void deallocate(T* memory, std::size_t count) noexcept
        {
            if (!resource_)
            {
                heap_allocator<T>{}.deallocate(memory, count);
            }
        }

        template<typename U, typename... Args>
        void construct(U* memory, Args&&... args)
        {
            std::apply([memory](auto&&... values)
                       { std::construct_at(memory, std::forward<decltype(values)>(values)...); },
                       std::uses_allocator_construction_args<U>(*this, std::forward<Args>(args)...));
        }

        template<typename U>
        bool operator==(const arena_allocator<U>& other) const noexcept
        {
            return resource_ == other.resource();
        }

      private:
        arena_resource* resource_{};
    };

    template<typename T>
    using arena_vector = std::vector<T, arena_allocator<T>>;

    template<typename Key, typename Value, typename Compare = std::less<Key>>
    using arena_map = std::map<Key, Value, Compare, arena_allocator<std::pair<const Key, Value>>>;

} // namespace ce
