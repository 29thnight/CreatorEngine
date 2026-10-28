#pragma once
#include "../values/value_types.h"
#include <iterator>
#include <algorithm>
#include <memory>
#include <functional>
namespace ce
{
    // AVL nodes never move. Erase transplants links, never another key's value.
    // Equal keys enter at upper_bound, retaining insertion order for Multi.
    template<class Key, class Compare = std::less<Key>, bool Multi = false>
    class ordered_node_set
    {
        struct link
        {
            link* parent = nullptr;
            link* left = nullptr;
            link* right = nullptr;
            int height = 1;
        };

        struct node : link
        {
            Key value;
            template<class... Args>
            explicit node(Args&&... args) : value(std::forward<Args>(args)...) {}
        };

        link header_;
        size_t size_ = 0;
        [[no_unique_address]] Compare compare_{};

        static node* as_node(link* p) noexcept { return static_cast<node*>(p); }
        static const Key& key(const link* p) noexcept { return static_cast<const node*>(p)->value; }
        static int height(const link* p) noexcept { return p ? p->height : 0; }
        static link* minimum(link* p) noexcept { while (p->left) p = p->left; return p; }
        static link* maximum(link* p) noexcept { while (p->right) p = p->right; return p; }

        static link* next(link* p) noexcept
        {
            if (p->right) return minimum(p->right);
            auto* parent = p->parent;
            while (parent->height != 0 && p == parent->right) { p = parent; parent = parent->parent; }
            return parent;
        }

        static link* previous(link* p) noexcept
        {
            if (p->height == 0) return p->right;
            if (p->left) return maximum(p->left);
            auto* parent = p->parent;
            while (parent->height != 0 && p == parent->left) { p = parent; parent = parent->parent; }
            return parent;
        }

        void reset_header() noexcept
        {
            header_.parent = nullptr;
            header_.left = header_.right = &header_;
            header_.height = 0;
        }

        void refresh_edges() noexcept
        {
            if (header_.parent)
            {
                header_.parent->parent = &header_;
                header_.left = minimum(header_.parent);
                header_.right = maximum(header_.parent);
            }
            else header_.left = header_.right = &header_;
        }

        static void update(link* p) noexcept
        {
            const int left = height(p->left), right = height(p->right);
            p->height = 1 + (left > right ? left : right);
        }

        void replace(link* old, link* replacement) noexcept
        {
            auto* parent = old->parent;
            if (parent == &header_) header_.parent = replacement;
            else if (parent->left == old) parent->left = replacement;
            else parent->right = replacement;
            if (replacement) replacement->parent = parent;
        }

        link* rotate_left(link* root) noexcept
        {
            auto* top = root->right;
            replace(root, top);
            root->right = top->left;
            if (root->right) root->right->parent = root;
            top->left = root;
            root->parent = top;
            update(root); update(top);
            return top;
        }

        link* rotate_right(link* root) noexcept
        {
            auto* top = root->left;
            replace(root, top);
            root->left = top->right;
            if (root->left) root->left->parent = root;
            top->right = root;
            root->parent = top;
            update(root); update(top);
            return top;
        }

        void rebalance(link* p) noexcept
        {
            while (p != &header_)
            {
                const int oldHeight = p->height;
                update(p);
                const int balance = height(p->left) - height(p->right);
                if (balance > 1)
                {
                    if (height(p->left->left) < height(p->left->right)) rotate_left(p->left);
                    p = rotate_right(p);
                }
                else if (balance < -1)
                {
                    if (height(p->right->right) < height(p->right->left)) rotate_right(p->right);
                    p = rotate_left(p);
                }
                if (p->height == oldHeight) break;
                p = p->parent;
            }
        }

        template<class... Args>
        node* make_node(Args&&... args)
        {
            node* memory;
            if constexpr (alignof(node) > alignof(std::max_align_t))
                memory = static_cast<node*>(::operator new(sizeof(node), std::align_val_t(alignof(node))));
            else memory = static_cast<node*>(::operator new(sizeof(node)));
            try { return std::construct_at(memory, std::forward<Args>(args)...); }
            catch (...) { deallocate_node(memory); throw; }
        }

        static void deallocate_node(node* memory) noexcept
        {
            if constexpr (alignof(node) > alignof(std::max_align_t))
                ::operator delete(memory, std::align_val_t(alignof(node)));
            else ::operator delete(memory);
        }

        void release(link* p) noexcept
        {
            auto* value = as_node(p);
            std::destroy_at(value);
            deallocate_node(value);
        }

        void destroy_branch(link* p) noexcept
        {
            if (!p) return;
            destroy_branch(p->left);
            destroy_branch(p->right);
            release(p);
        }

        struct position { link* parent; link* existing; bool left; };

        template<class Q>
        position locate(const Q& value) const
        {
            auto* parent = const_cast<link*>(&header_);
            auto* current = header_.parent;
            bool left = true;
            while (current)
            {
                parent = current;
                left = compare_(value, key(current));
                if (left) current = current->left;
                else
                {
                    if constexpr (!Multi)
                        if (!compare_(key(current), value)) return {parent, current, false};
                    current = current->right;
                }
            }
            return {parent, nullptr, left};
        }

        void attach(node* value, position where) noexcept
        {
            value->parent = where.parent;
            if (where.parent == &header_)
            {
                header_.parent = value;
                header_.left = header_.right = value;
            }
            else if (where.left) where.parent->left = value;
            else where.parent->right = value;
            if (where.left && where.parent == header_.left) header_.left = value;
            if (!where.left && where.parent == header_.right) header_.right = value;
            ++size_;
            rebalance(where.parent);
        }

        template<class Q>
        link* bound(const Q& value, bool upper) const
        {
            auto* result = const_cast<link*>(&header_);
            auto* current = header_.parent;
            // Only consult the cached maximum when the root directs the query
            // right. Left-half hits pay no extra extremum comparison.
            if (current)
            {
                const bool candidate = upper ? compare_(value, key(current)) : !compare_(key(current), value);
                if (candidate) { result = current; current = current->left; }
                else
                {
                    if (upper ? !compare_(value, key(header_.right)) : compare_(key(header_.right), value)) return result;
                    current = current->right;
                }
            }
            while (current)
            {
                const bool candidate = upper ? compare_(value, key(current)) : !compare_(key(current), value);
                if (candidate)
                {
                    result = current;
                    if (!current->left) return result;
                    current = current->left;
                }
                else
                {
                    if (!current->right) return result;
                    current = current->right;
                }
            }
            return result;
        }

    public:
        using key_type = Key;
        using value_type = Key;
        using key_compare = Compare;
        using value_compare = Compare;
        using size_type = size_t;

        class const_iterator
        {
            friend class ordered_node_set;
            link* position_ = nullptr;
            explicit const_iterator(link* p) : position_(p) {}

        public:
            using value_type = Key;
            using difference_type = std::ptrdiff_t;
            using reference = const Key&;
            using pointer = const Key*;
            using iterator_category = std::bidirectional_iterator_tag;
            using iterator_concept = std::bidirectional_iterator_tag;

            const_iterator() = default;
            reference operator*() const noexcept { return key(position_); }
            pointer operator->() const noexcept { return std::addressof(key(position_)); }
            const_iterator& operator++() noexcept { position_ = next(position_); return *this; }
            const_iterator operator++(int) noexcept { auto old = *this; ++*this; return old; }
            const_iterator& operator--() noexcept { position_ = previous(position_); return *this; }
            const_iterator operator--(int) noexcept { auto old = *this; --*this; return old; }
            bool operator==(const const_iterator&) const = default;
        };

        using iterator = const_iterator;
        using reverse_iterator = std::reverse_iterator<iterator>;
        using const_reverse_iterator = reverse_iterator;

        ordered_node_set() { reset_header(); }
        explicit ordered_node_set(const Compare& compare) : compare_(compare) { reset_header(); }

        template<std::input_iterator It>
        ordered_node_set(It first, It last, const Compare& compare = Compare()) : ordered_node_set(compare)
        {
            try { insert(first, last); }
            catch (...) { clear(); throw; }
        }

        ordered_node_set(std::initializer_list<Key> values, const Compare& compare = Compare())
            : ordered_node_set(values.begin(), values.end(), compare) {}

        ordered_node_set(const ordered_node_set& other) : ordered_node_set(other.begin(), other.end(), other.compare_) {}

        ordered_node_set(ordered_node_set&& other) noexcept(std::is_nothrow_move_constructible_v<Compare>)
            : compare_(std::move(other.compare_))
        {
            reset_header();
            header_.parent = other.header_.parent;
            size_ = std::exchange(other.size_, 0);
            refresh_edges();
            other.reset_header();
        }

        ~ordered_node_set() { clear(); }

        ordered_node_set& operator=(const ordered_node_set& other)
        {
            if (this != &other) { ordered_node_set fresh(other); swap(fresh); }
            return *this;
        }

        ordered_node_set& operator=(ordered_node_set&& other) noexcept(std::is_nothrow_move_constructible_v<Compare> && std::is_nothrow_swappable_v<Compare>)
        {
            if (this != &other) { ordered_node_set fresh(std::move(other)); swap(fresh); }
            return *this;
        }

        ordered_node_set& operator=(std::initializer_list<Key> values)
        {
            ordered_node_set fresh(values, compare_); swap(fresh); return *this;
        }

        void swap(ordered_node_set& other) noexcept
        {
            static_assert(std::is_nothrow_swappable_v<Compare>, "Comparator swap must preserve ownership on failure");
            using std::swap;
            swap(compare_, other.compare_);
            swap(header_.parent, other.header_.parent);
            swap(size_, other.size_);
            refresh_edges(); other.refresh_edges();
        }

        void clear() noexcept { destroy_branch(header_.parent); size_ = 0; reset_header(); }
        bool empty() const noexcept { return size_ == 0; }
        size_t size() const noexcept { return size_; }
        Compare key_comp() const { return compare_; }
        Compare value_comp() const { return compare_; }
        iterator begin() const noexcept { return iterator(header_.left); }
        iterator end() const noexcept { return iterator(const_cast<link*>(&header_)); }
        iterator cbegin() const noexcept { return begin(); }
        iterator cend() const noexcept { return end(); }
        reverse_iterator rbegin() const noexcept { return reverse_iterator(end()); }
        reverse_iterator rend() const noexcept { return reverse_iterator(begin()); }
        reverse_iterator crbegin() const noexcept { return rbegin(); }
        reverse_iterator crend() const noexcept { return rend(); }

        template<class Q> iterator lower_bound(const Q& value) const { return iterator(bound(value, false)); }
        template<class Q> iterator upper_bound(const Q& value) const { return iterator(bound(value, true)); }
        template<class Q> ce::pair<iterator, iterator> equal_range(const Q& value) const { return {lower_bound(value), upper_bound(value)}; }

        template<class Q>
        iterator find(const Q& value) const
        {
            auto found = lower_bound(value);
            return found != end() && !compare_(value, *found) ? found : end();
        }

        template<class Q> bool contains(const Q& value) const { return find(value) != end(); }

        template<class Q>
        size_t count(const Q& value) const
        {
            if constexpr (!Multi) return contains(value) ? 1 : 0;
            else { auto range = equal_range(value); return static_cast<size_t>(std::distance(range.first, range.second)); }
        }

        template<class K>
        auto insert_value(K&& value)
        {
            auto where = locate(value);
            if constexpr (!Multi)
            {
                if (where.existing) return ce::pair<iterator, bool>{iterator(where.existing), false};
                auto* fresh = make_node(std::forward<K>(value));
                attach(fresh, where);
                return ce::pair<iterator, bool>{iterator(fresh), true};
            }
            else
            {
                auto* fresh = make_node(std::forward<K>(value));
                attach(fresh, where);
                return iterator(fresh);
            }
        }

        auto insert(const Key& value) { return insert_value(value); }
        auto insert(Key&& value) { return insert_value(std::move(value)); }
        template<std::input_iterator It> void insert(It first, It last) { for (; first != last; ++first) insert(*first); }
        void insert(std::initializer_list<Key> values) { insert(values.begin(), values.end()); }

        template<class... Args>
        auto emplace(Args&&... args)
        {
            // Construct once in its final node, including move-only keys.
            auto* fresh = make_node(std::forward<Args>(args)...);
            position where;
            try { where = locate(fresh->value); }
            catch (...) { release(fresh); throw; }
            if constexpr (!Multi)
            {
                if (where.existing) { release(fresh); return ce::pair<iterator, bool>{iterator(where.existing), false}; }
                attach(fresh, where);
                return ce::pair<iterator, bool>{iterator(fresh), true};
            }
            else { attach(fresh, where); return iterator(fresh); }
        }

        iterator insert(const_iterator hint, const Key& value) requires (!Multi)
        {
            (void)hint;
            return insert(value).first;
        }

        iterator insert(const_iterator hint, Key&& value) requires (!Multi)
        {
            (void)hint;
            return insert(std::move(value)).first;
        }

        iterator erase(const_iterator position) noexcept
        {
            auto* erased = position.position_;
            auto result = iterator(next(erased));
            auto* start = erased->parent;
            if (!erased->left) replace(erased, erased->right);
            else if (!erased->right) replace(erased, erased->left);
            else
            {
                auto* successor = minimum(erased->right);
                if (successor->parent != erased)
                {
                    start = successor->parent;
                    replace(successor, successor->right);
                    successor->right = erased->right;
                    successor->right->parent = successor;
                }
                else start = successor;
                replace(erased, successor);
                successor->left = erased->left;
                successor->left->parent = successor;
                successor->height = erased->height;
            }
            --size_;
            rebalance(start);
            refresh_edges();
            release(erased);
            return result;
        }

        iterator erase(const_iterator first, const_iterator last) noexcept
        {
            while (first != last) first = erase(first);
            return first;
        }

        template<class Q>
        size_t erase(const Q& value)
        {
            auto range = equal_range(value);
            size_t erased = 0;
            while (range.first != range.second) { range.first = erase(range.first); ++erased; }
            return erased;
        }

        friend void swap(ordered_node_set& a, ordered_node_set& b) noexcept { a.swap(b); }
        friend bool operator==(const ordered_node_set& a, const ordered_node_set& b)
        {
            return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin());
        }
        friend auto operator<=>(const ordered_node_set& a, const ordered_node_set& b)
            requires std::three_way_comparable<Key>
        {
            return std::lexicographical_compare_three_way(a.begin(), a.end(), b.begin(), b.end());
        }
    };
}
