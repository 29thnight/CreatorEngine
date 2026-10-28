#pragma once
#include "../include/ce/dynamic_array.h"
#include "../include/ce/hash.h"
#include <initializer_list>
#include <string.h>
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4324) // Node/key-value padding is required for over-aligned values.
#endif
namespace ce
{
    template<class Key, class Value>
    struct key_value
    {
        const Key first;
        Value second;

        template<class K, class... A>
            requires(__is_constructible(Key, K &&) && __is_constructible(Value, A && ...))
        key_value(K&& key, A&&... args) : first(static_cast<K&&>(key)), second(static_cast<A&&>(args)...)
        {
        }
    };

    // Stable pooled nodes with a compact control-byte index. PoolNodes=false
    // preserves per-value deallocation for allocator policies requiring it.
    template<class Key, class Value, class Hash = hash<Key>, class Equal = equal_to<Key>,
             class Allocator = heap_allocator, bool PoolNodes = true>
    class node_hash_map
    {
        static_assert(detail::nothrow_policy<Hash> && detail::nothrow_policy<Equal> &&
                      detail::nothrow_policy<Allocator>);
        static_assert(detail::nothrow_destroy<Key> && detail::nothrow_destroy<Value>);

      public:
        using key_type = Key;
        using mapped_type = Value;
        using value_type = key_value<Key, Value>;
        using size_type = size_t;
        using hasher = Hash;
        using key_equal = Equal;
        using allocator_type = Allocator;

      private:
        static constexpr size_t absent = SIZE_MAX;

        struct index_storage
        {
            Allocator alloc;
            void* raw = nullptr;
            size_t capacity = 0;

            explicit index_storage(const Allocator& a) : alloc(a)
            {
            }

            index_storage(const index_storage&) = delete;

            index_storage(index_storage&& other) noexcept : alloc(other.alloc), raw(other.raw), capacity(other.capacity)
            {
                other.raw = nullptr;
                other.capacity = 0;
            }

            ~index_storage()
            {
                if (raw)
                    alloc.deallocate(raw, capacity * (sizeof(value_type*) + 1), alignof(value_type*));
            }

            value_type** nodes() const noexcept
            {
                return static_cast<value_type**>(raw);
            }

            unsigned char* controls() const noexcept
            {
                return reinterpret_cast<unsigned char*>(nodes() + capacity);
            }

            void allocate(size_t count)
            {
                raw = alloc.allocate(detail::checked_bytes(count, sizeof(value_type*) + 1), alignof(value_type*));
                capacity = count;
                memset(controls(), 0, count);
            }

            void swap(index_storage& other) noexcept
            {
                auto a = alloc;
                alloc = other.alloc;
                other.alloc = a;
                auto p = raw;
                raw = other.raw;
                other.raw = p;
                auto n = capacity;
                capacity = other.capacity;
                other.capacity = n;
            }

            void store(size_t i, value_type* node, unsigned char control) noexcept
            {
                ::new (static_cast<void*>(nodes() + i)) value_type*(node);
                controls()[i] = control;
            }
        };

        union cell
        {
            value_type value;
            cell* next;

            cell() noexcept
            {
            }

            ~cell() noexcept
            {
            }
        };

        static constexpr size_t block_cells = sizeof(cell) <= 256 ? 64 : (sizeof(cell) <= 4096 ? 16 : 1);

        struct block
        {
            block* next = nullptr;
            size_t used = 0;
            cell cells[block_cells];
        };

        struct pending_node
        {
            cell* location = nullptr;
            cell* next_free = nullptr;
            block* new_block = nullptr;
            void* allocation = nullptr;
            bool reused = false;
        };

        CE_NO_UNIQUE_ADDRESS Allocator alloc_;
        CE_NO_UNIQUE_ADDRESS Hash hash_;
        CE_NO_UNIQUE_ADDRESS Equal equal_;
        index_storage index_;
        block* blocks_ = nullptr;
        cell* free_ = nullptr;
        size_t size_ = 0, erased_ = 0, limit_ = 0;
        float max_load_ = 0.75f;

        static unsigned char fingerprint(uint64_t code) noexcept
        {
            return static_cast<unsigned char>((code >> 57) | 0x80);
        }

        size_t threshold(size_t capacity) const noexcept
        {
            if (!capacity)
                return 0;
            const auto factor = max_load_ < 0.875f ? max_load_ : 0.875f;
            auto result = size_t(double(capacity) * factor);
            return result < capacity ? result : capacity - 1;
        }

        template<class Q>
        size_t locate(const Q& key, uint64_t code) const
        {
            if (!index_.capacity)
                return absent;
            auto i = size_t(code) & (index_.capacity - 1);
            auto wanted = fingerprint(code);
            auto* control = index_.controls();
            for (size_t checked = 0; checked < index_.capacity; ++checked)
            {
                auto c = control[i];
                if (!c)
                    return absent;
                if (c == wanted && equal_(index_.nodes()[i]->first, key))
                    return i;
                i = (i + 1) & (index_.capacity - 1);
            }
            return absent;
        }

        struct probe_result
        {
            size_t existing;
            size_t insertion;
        };

        template<class Q>
        probe_result probe(const Q& key, uint64_t code) const
        {
            if (!index_.capacity)
                return {absent, absent};
            auto i = size_t(code) & (index_.capacity - 1);
            auto wanted = fingerprint(code);
            size_t free = absent;
            auto* control = index_.controls();
            for (size_t checked = 0; checked < index_.capacity; ++checked)
            {
                auto c = control[i];
                if (c < 2)
                {
                    if (free == absent)
                        free = i;
                    if (!c)
                        return {absent, free};
                }
                else if (c == wanted && equal_(index_.nodes()[i]->first, key))
                    return {i, absent};
                i = (i + 1) & (index_.capacity - 1);
            }
            return {absent, free};
        }

        static size_t empty_position(const index_storage& index, uint64_t code) noexcept
        {
            auto i = size_t(code) & (index.capacity - 1);
            while (index.controls()[i] >= 2)
                i = (i + 1) & (index.capacity - 1);
            return i;
        }

        index_storage prepare_index(size_t capacity) const
        {
            index_storage fresh(alloc_);
            fresh.allocate(capacity);
            for (size_t i = 0; i < index_.capacity; ++i)
                if (index_.controls()[i] >= 2)
                {
                    auto* node = index_.nodes()[i];
                    auto code = hash_(node->first);
                    fresh.store(empty_position(fresh, code), node, fingerprint(code));
                }
            return fresh;
        }

        size_t prepared_capacity() const
        {
            if (size_ >= max_size())
                throw std::bad_alloc{};
            if (!index_.capacity)
            {
                size_t capacity = 8;
                while (size_ + 1 > threshold(capacity))
                {
                    if (capacity > max_size() / 2)
                        throw std::bad_alloc{};
                    capacity *= 2;
                }
                return capacity;
            }
            if (size_ + 1 > limit_)
            {
                auto capacity = index_.capacity;
                do
                {
                    if (capacity > max_size() / 2)
                        throw std::bad_alloc{};
                    capacity *= 2;
                } while (size_ + 1 > threshold(capacity));
                return capacity;
            }
            if (erased_ > index_.capacity / 8 && size_ + erased_ + 1 >= index_.capacity - index_.capacity / 8)
                return index_.capacity;
            return 0;
        }

        pending_node prepare_node()
        {
            pending_node p;
            if constexpr (!PoolNodes)
            {
                p.allocation = alloc_.allocate(sizeof(value_type), alignof(value_type));
                return p;
            }
            else
            {
                if (free_)
                {
                    p.location = free_;
                    p.next_free = free_->next;
                    p.reused = true;
                    return p;
                }
                if (blocks_ && blocks_->used < block_cells)
                {
                    p.location = blocks_->cells + blocks_->used;
                    return p;
                }
                auto* memory = alloc_.allocate(sizeof(block), alignof(block));
                p.new_block = ::new (memory) block;
                p.location = p.new_block->cells;
                return p;
            }
        }

        void rollback_node(pending_node& p) noexcept
        {
            if constexpr (!PoolNodes)
                alloc_.deallocate(p.allocation, sizeof(value_type), alignof(value_type));
            else if (p.new_block)
            {
                p.new_block->~block();
                alloc_.deallocate(p.new_block, sizeof(block), alignof(block));
            }
            else if (p.reused)
                ::new (static_cast<void*>(&p.location->next)) cell*(p.next_free);
        }

        void commit_node(pending_node& p) noexcept
        {
            if constexpr (PoolNodes)
            {
                if (p.new_block)
                {
                    p.new_block->next = blocks_;
                    p.new_block->used = 1;
                    blocks_ = p.new_block;
                }
                else if (p.reused)
                    free_ = p.next_free;
                else
                    ++blocks_->used;
            }
        }

        void release(value_type* node) noexcept
        {
            node->~value_type();
            if constexpr (PoolNodes)
            {
                auto* entry = reinterpret_cast<cell*>(node);
                ::new (static_cast<void*>(&entry->next)) cell*(free_);
                free_ = entry;
            }
            else
                alloc_.deallocate(node, sizeof(value_type), alignof(value_type));
        }

        void erase_index(size_t i) noexcept
        {
            auto* node = index_.nodes()[i];
            index_.controls()[i] = 1;
            --size_;
            ++erased_;
            release(node);
        }

      public:
        template<bool Constant>
        class basic_iterator
        {
            friend class node_hash_map;
            template<bool>
            friend class basic_iterator;
            using owner_type=decltype([]{if constexpr(Constant)return static_cast<const node_hash_map*>(nullptr);else return static_cast<node_hash_map*>(nullptr);}());
            owner_type owner_ = nullptr;
            size_t index_ = 0;

            basic_iterator(owner_type owner, size_t i) : owner_(owner), index_(i)
            {
                skip();
            }

            void skip()
            {
                while (owner_ && index_ < owner_->index_.capacity && owner_->index_.controls()[index_] < 2)
                    ++index_;
            }

          public:
            using value_type = key_value<Key, Value>;
            using difference_type = ptrdiff_t;
            using pointer=decltype([]{if constexpr(Constant)return static_cast<const value_type*>(nullptr);else return static_cast<value_type*>(nullptr);}());
            using reference = decltype(*static_cast<pointer>(nullptr));
            basic_iterator() = default;

            template<bool C = Constant>
                requires(C)
            basic_iterator(const basic_iterator<false>& other) : owner_(other.owner_), index_(other.index_)
            {
            }

            reference operator*() const
            {
                return *owner_->index_.nodes()[index_];
            }

            pointer operator->() const
            {
                return owner_->index_.nodes()[index_];
            }

            basic_iterator& operator++()
            {
                ++index_;
                skip();
                return *this;
            }

            basic_iterator operator++(int)
            {
                auto old = *this;
                ++*this;
                return old;
            }

            bool operator==(const basic_iterator& other) const
            {
                return owner_ == other.owner_ && index_ == other.index_;
            }
        };

        using iterator = basic_iterator<false>;
        using const_iterator = basic_iterator<true>;

        struct insertion_result
        {
            iterator first;
            bool second;
        };

        node_hash_map() : index_(alloc_)
        {
        }

        explicit node_hash_map(const Allocator& alloc) : alloc_(alloc), index_(alloc_)
        {
        }

        explicit node_hash_map(size_t buckets, const Hash& hash = Hash{}, const Equal& equal = Equal{},
                               const Allocator& alloc = Allocator{})
            : alloc_(alloc), hash_(hash), equal_(equal), index_(alloc_)
        {
            if (buckets)
                rehash(buckets);
        }

        node_hash_map(std::initializer_list<value_type> values, size_t buckets = 0, const Hash& hash = Hash{},
                      const Equal& equal = Equal{}, const Allocator& alloc = Allocator{})
            : node_hash_map(buckets, hash, equal, alloc)
        {
            insert(values.begin(), values.end());
        }

        template<class It>
            requires requires(It i) {
                *i;
                ++i;
                i != i;
            }
        node_hash_map(It first, It last, size_t buckets = 0, const Hash& hash = Hash{}, const Equal& equal = Equal{},
                      const Allocator& alloc = Allocator{})
            : node_hash_map(buckets, hash, equal, alloc)
        {
            insert(first, last);
        }

        node_hash_map(const node_hash_map& other)
            requires(detail::copyable<Key> && detail::copyable<Value>)
            : node_hash_map(0, other.hash_, other.equal_, other.alloc_)
        {
            max_load_ = other.max_load_;
            reserve(other.size_);
            insert(other.begin(), other.end());
        }

        node_hash_map(node_hash_map&& other) noexcept
            : alloc_(other.alloc_), hash_(other.hash_), equal_(other.equal_),
              index_(static_cast<index_storage&&>(other.index_)), blocks_(other.blocks_), free_(other.free_),
              size_(other.size_), erased_(other.erased_), limit_(other.limit_), max_load_(other.max_load_)
        {
            other.blocks_ = nullptr;
            other.free_ = nullptr;
            other.size_ = other.erased_ = other.limit_ = 0;
        }

        ~node_hash_map()
        {
            clear();
        }

        node_hash_map& operator=(const node_hash_map& other)
            requires(detail::copyable<Key> && detail::copyable<Value>)
        {
            if (this != &other)
            {
                node_hash_map fresh(other);
                swap(fresh);
            }
            return *this;
        }

        node_hash_map& operator=(node_hash_map&& other) noexcept
        {
            if (this != &other)
            {
                node_hash_map fresh(static_cast<node_hash_map&&>(other));
                swap(fresh);
            }
            return *this;
        }

        node_hash_map& operator=(std::initializer_list<value_type> values)
        {
            node_hash_map fresh(values, 0, hash_, equal_, alloc_);
            swap(fresh);
            return *this;
        }

        void swap(node_hash_map& other) noexcept
        {
            auto a = alloc_;
            alloc_ = other.alloc_;
            other.alloc_ = a;
            auto h = hash_;
            hash_ = other.hash_;
            other.hash_ = h;
            auto e = equal_;
            equal_ = other.equal_;
            other.equal_ = e;
            index_.swap(other.index_);
            auto b = blocks_;
            blocks_ = other.blocks_;
            other.blocks_ = b;
            auto f = free_;
            free_ = other.free_;
            other.free_ = f;
            auto n = size_;
            size_ = other.size_;
            other.size_ = n;
            n = erased_;
            erased_ = other.erased_;
            other.erased_ = n;
            n = limit_;
            limit_ = other.limit_;
            other.limit_ = n;
            auto l = max_load_;
            max_load_ = other.max_load_;
            other.max_load_ = l;
        }

        void clear() noexcept
        {
            if (size_)
            {
                for (size_t i = 0; i < index_.capacity; ++i)
                    if (index_.controls()[i] >= 2)
                    {
                        auto* node = index_.nodes()[i];
                        node->~value_type();
                        if constexpr (!PoolNodes)
                            alloc_.deallocate(node, sizeof(value_type), alignof(value_type));
                    }
            }
            if (size_ || erased_)
                memset(index_.controls(), 0, index_.capacity);
            while (blocks_)
            {
                auto* next = blocks_->next;
                blocks_->~block();
                alloc_.deallocate(blocks_, sizeof(block), alignof(block));
                blocks_ = next;
            }
            free_ = nullptr;
            size_ = erased_ = 0;
        }

        bool empty() const noexcept
        {
            return !size_;
        }

        size_t size() const noexcept
        {
            return size_;
        }

        size_t max_size() const noexcept
        {
            constexpr size_t a = size_t(PTRDIFF_MAX) / (sizeof(value_type*) + 1), b = SIZE_MAX / sizeof(value_type);
            return a < b ? a : b;
        }

        size_t bucket_count() const noexcept
        {
            return index_.capacity;
        }

        float load_factor() const noexcept
        {
            return index_.capacity ? float(size_) / float(index_.capacity) : 0.f;
        }

        float max_load_factor() const noexcept
        {
            return max_load_;
        }

        void max_load_factor(float value)
        {
            if (!(value > 0))
                throw bounds_error{};
            max_load_ = value;
            limit_ = threshold(index_.capacity);
        }

        Hash hash_function() const noexcept
        {
            return hash_;
        }

        Equal key_eq() const noexcept
        {
            return equal_;
        }

        Allocator get_allocator() const noexcept
        {
            return alloc_;
        }

        void reserve(size_t elements)
        {
            if (elements > max_size())
                throw std::bad_alloc{};
            size_t capacity = index_.capacity ? index_.capacity : 8;
            while (elements > threshold(capacity))
            {
                if (capacity > max_size() / 2)
                    throw std::bad_alloc{};
                capacity *= 2;
            }
            if (capacity > index_.capacity)
                rehash(capacity);
        }

        void rehash(size_t requested)
        {
            size_t capacity = 8;
            while (capacity < requested || size_ > threshold(capacity))
            {
                if (capacity > max_size() / 2)
                    throw std::bad_alloc{};
                capacity *= 2;
            }
            auto fresh = prepare_index(capacity);
            index_.swap(fresh);
            limit_ = threshold(capacity);
            erased_ = 0;
        }

        iterator begin() noexcept
        {
            return {this, 0};
        }

        iterator end() noexcept
        {
            return {this, index_.capacity};
        }

        const_iterator begin() const noexcept
        {
            return {this, 0};
        }

        const_iterator end() const noexcept
        {
            return {this, index_.capacity};
        }

        const_iterator cbegin() const noexcept
        {
            return begin();
        }

        const_iterator cend() const noexcept
        {
            return end();
        }

        template<class Q>
        iterator find(const Q& key)
        {
            auto i = locate(key, hash_(key));
            return {this, i == absent ? index_.capacity : i};
        }

        template<class Q>
        const_iterator find(const Q& key) const
        {
            auto i = locate(key, hash_(key));
            return {this, i == absent ? index_.capacity : i};
        }

        template<class Q>
        bool contains(const Q& key) const
        {
            return locate(key, hash_(key)) != absent;
        }

        template<class Q>
        size_t count(const Q& key) const
        {
            return contains(key) ? 1 : 0;
        }

        template<class Q>
        Value& at(const Q& key)
        {
            auto found = find(key);
            if (found == end())
                throw bounds_error{};
            return found->second;
        }

        template<class Q>
        const Value& at(const Q& key) const
        {
            auto found = find(key);
            if (found == end())
                throw bounds_error{};
            return found->second;
        }

        template<class K, class... A>
        insertion_result try_emplace(K&& key, A&&... args)
        {
            auto code = hash_(key);
            auto position = probe(key, code);
            if (position.existing != absent)
                return {{this, position.existing}, false};
            const auto capacity = prepared_capacity();
            index_storage prepared(alloc_);
            if (capacity)
            {
                auto fresh = prepare_index(capacity);
                prepared.swap(fresh);
            }
            auto pending = prepare_node();
            value_type* node;
            try
            {
                void* address;
                if constexpr (PoolNodes)
                    address = static_cast<void*>(&pending.location->value);
                else
                    address = pending.allocation;
                node = ::new (address) value_type(static_cast<K&&>(key), static_cast<A&&>(args)...);
            }
            catch (...)
            {
                rollback_node(pending);
                throw;
            }
            commit_node(pending);
            if (capacity)
            {
                index_.swap(prepared);
                limit_ = threshold(capacity);
                erased_ = 0;
                position.insertion = empty_position(index_, code);
            }
            auto i = position.insertion;
            if (index_.controls()[i] == 1)
                --erased_;
            index_.store(i, node, fingerprint(code));
            ++size_;
            return {{this, i}, true};
        }

        template<class K, class V>
        insertion_result insert_or_assign(K&& key, V&& value)
        {
            auto found = find(key);
            if (found != end())
            {
                found->second = static_cast<V&&>(value);
                return {found, false};
            }
            return try_emplace(static_cast<K&&>(key), static_cast<V&&>(value));
        }

        Value& operator[](const Key& key)
        {
            return try_emplace(key).first->second;
        }

        Value& operator[](Key&& key)
        {
            return try_emplace(static_cast<Key&&>(key)).first->second;
        }

        insertion_result insert(const value_type& value)
        {
            return try_emplace(value.first, value.second);
        }

        insertion_result insert(value_type&& value)
        {
            return try_emplace(value.first, static_cast<Value&&>(value.second));
        }

        template<class P>
            requires requires(P p) {
                p.first;
                p.second;
            }
        insertion_result insert(P&& value)
        {
            return try_emplace(static_cast<P&&>(value).first, static_cast<P&&>(value).second);
        }

        template<class It>
        void insert(It first, It last)
        {
            for (; first != last; ++first)
                insert(*first);
        }

        void insert(std::initializer_list<value_type> values)
        {
            insert(values.begin(), values.end());
        }

        template<class K, class V>
        insertion_result emplace(K&& key, V&& value)
        {
            return try_emplace(static_cast<K&&>(key), static_cast<V&&>(value));
        }

        template<class P>
        insertion_result emplace(P&& value)
        {
            return insert(static_cast<P&&>(value));
        }

        iterator erase(const_iterator position)
        {
            auto next = iterator(this, position.index_);
            ++next;
            erase_index(position.index_);
            return next;
        }

        iterator erase(iterator position)
        {
            return erase(const_iterator(position));
        }

        iterator erase(const_iterator first, const_iterator last)
        {
            while (first != last)
                first = erase(first);
            return {this, last.index_};
        }

        template<class Q>
        size_t erase(const Q& key)
        {
            auto i = locate(key, hash_(key));
            if (i == absent)
                return 0;
            erase_index(i);
            return 1;
        }

        bool operator==(const node_hash_map& other) const
        {
            if (size_ != other.size_)
                return false;
            for (const auto& entry : *this)
            {
                auto found = other.find(entry.first);
                if (found == other.end() || !(entry.second == found->second))
                    return false;
            }
            return true;
        }
    };
} // namespace ce
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
