#pragma once

#include "EngineNodeMap.h"

namespace ce
{
    // Policy adapter over the accepted non-pooled node implementation.
    // Keys are owned and immutable; rehash preserves references, not iterators.
    template<class Key, class Hash = engine_node_hash<Key>, class Equal = engine_node_equal<Key>>
    class engine_node_set
    {
        struct present {};
        using storage_type = node_hash_map<Key, present, Hash, Equal, heap_allocator, false>;
        storage_type storage_;

    public:
        using key_type = Key;
        using value_type = Key;
        using size_type = size_t;
        using hasher = Hash;
        using key_equal = Equal;

        class const_iterator
        {
            friend class engine_node_set;
            typename storage_type::const_iterator position_;

            explicit const_iterator(typename storage_type::const_iterator position) : position_(position) {}

        public:
            using value_type = Key;
            using difference_type = ptrdiff_t;
            using pointer = const Key*;
            using reference = const Key&;
            using iterator_category = std::forward_iterator_tag;
            using iterator_concept = std::forward_iterator_tag;

            const_iterator() = default;
            reference operator*() const { return position_->first; }
            pointer operator->() const { return &position_->first; }

            const_iterator& operator++()
            {
                ++position_;
                return *this;
            }

            const_iterator operator++(int)
            {
                auto previous = *this;
                ++*this;
                return previous;
            }

            bool operator==(const const_iterator&) const = default;
        };

        using iterator = const_iterator;

        engine_node_set() = default;

        engine_node_set(std::initializer_list<Key> values)
        {
            reserve(values.size());
            insert(values.begin(), values.end());
        }

        template<std::input_iterator It>
        engine_node_set(It first, It last) { insert(first, last); }

        engine_node_set& operator=(std::initializer_list<Key> values)
        {
            engine_node_set replacement(values);
            swap(replacement);
            return *this;
        }

        bool empty() const noexcept { return storage_.empty(); }
        size_t size() const noexcept { return storage_.size(); }
        void clear() noexcept { storage_.clear(); }
        void reserve(size_t count) { storage_.reserve(count); }
        void rehash(size_t count) { storage_.rehash(count); }
        void swap(engine_node_set& other) noexcept { storage_.swap(other.storage_); }

        iterator begin() noexcept { return iterator(storage_.cbegin()); }
        iterator end() noexcept { return iterator(storage_.cend()); }
        const_iterator begin() const noexcept { return const_iterator(storage_.begin()); }
        const_iterator end() const noexcept { return const_iterator(storage_.end()); }
        const_iterator cbegin() const noexcept { return begin(); }
        const_iterator cend() const noexcept { return end(); }

        template<class Q>
        iterator find(const Q& key) { return iterator(storage_.find(key)); }

        template<class Q>
        const_iterator find(const Q& key) const { return const_iterator(storage_.find(key)); }

        template<class Q>
        bool contains(const Q& key) const { return storage_.contains(key); }

        template<class Q>
        size_t count(const Q& key) const { return storage_.count(key); }

        ce::pair<iterator, bool> insert(const Key& key)
        {
            auto result = storage_.try_emplace(key);
            return {iterator(result.first), result.second};
        }

        ce::pair<iterator, bool> insert(Key&& key)
        {
            auto result = storage_.try_emplace(std::move(key));
            return {iterator(result.first), result.second};
        }

        template<std::input_iterator It>
        void insert(It first, It last)
        {
            for (; first != last; ++first)
                insert(*first);
        }

        void insert(std::initializer_list<Key> values) { insert(values.begin(), values.end()); }

        template<class... Args>
        ce::pair<iterator, bool> emplace(Args&&... args)
        {
            return insert(Key(std::forward<Args>(args)...));
        }

        iterator erase(const_iterator position) { return iterator(storage_.erase(position.position_)); }

        iterator erase(const_iterator first, const_iterator last)
        {
            return iterator(storage_.erase(first.position_, last.position_));
        }

        template<class Q>
        size_t erase(const Q& key) { return storage_.erase(key); }

        friend bool operator==(const engine_node_set& left, const engine_node_set& right)
        {
            if (left.size() != right.size())
                return false;

            for (const auto& key : left)
                if (!right.contains(key))
                    return false;

            return true;
        }

        friend void swap(engine_node_set& left, engine_node_set& right) noexcept { left.swap(right); }
    };

    template<class Key, class Hash, class Equal, class Predicate>
    size_t erase_if(engine_node_set<Key, Hash, Equal>& values, Predicate predicate)
    {
        const auto before = values.size();
        for (auto it = values.begin(); it != values.end();)
        {
            if (predicate(*it))
                it = values.erase(it);
            else
                ++it;
        }
        return before - values.size();
    }
}
