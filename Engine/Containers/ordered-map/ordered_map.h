#pragma once
#include "../values/value_types.h"
#include <iterator>
#include <algorithm>
#include <memory>
#include <functional>
#include <optional>
#include <stdexcept>
namespace ce
{
#if defined(_MSC_VER)
#define CE_ORDERED_MAP_INLINE __forceinline
#else
#define CE_ORDERED_MAP_INLINE inline __attribute__((always_inline))
#endif
    template<class T>
    struct ordered_heap_allocator
    {
        using value_type = T;
        using is_always_equal = std::true_type;
        using propagate_on_container_move_assignment = std::true_type;
        ordered_heap_allocator() noexcept = default;
        template<class U> ordered_heap_allocator(const ordered_heap_allocator<U>&) noexcept {}
        T* allocate(size_t count)
        {
            if (count > size_t(-1) / sizeof(T)) throw std::bad_array_new_length();
            if constexpr (alignof(T) > alignof(std::max_align_t))
                return static_cast<T*>(::operator new(count * sizeof(T), std::align_val_t(alignof(T))));
            else return static_cast<T*>(::operator new(count * sizeof(T)));
        }
        void deallocate(T* memory, size_t) noexcept
        {
            if constexpr (alignof(T) > alignof(std::max_align_t)) ::operator delete(memory, std::align_val_t(alignof(T)));
            else ::operator delete(memory);
        }
        template<class U> bool operator==(const ordered_heap_allocator<U>&) const noexcept { return true; }
    };

    template<class Key, class Mapped, class Compare = std::less<Key>,
             class Allocator = ordered_heap_allocator<ce::pair<const Key, Mapped>>, bool Multi = false>
    class ordered_node_map
    {
        using traits = std::allocator_traits<Allocator>;
        using stored_type = typename traits::value_type;
        template<class Q> static constexpr bool lookup_key = std::is_same_v<std::remove_cvref_t<Q>, Key> || requires { typename Compare::is_transparent; };
        static constexpr bool default_heap = std::is_same_v<Allocator, ordered_heap_allocator<stored_type>>;
        static_assert(std::is_nothrow_copy_constructible_v<Allocator>);
        static_assert(std::is_nothrow_move_constructible_v<Allocator> && std::is_nothrow_move_assignable_v<Allocator>);

        enum : int { sentinel_color = 0, black = 1, red = 2 };
        struct link
        {
            link* parent = nullptr;
            link* left = nullptr;
            link* right = nullptr;
            int color = red;
        };
        struct node : link
        {
            union { stored_type value; };
            node() noexcept {}
            ~node() noexcept {}
        };
        using node_allocator = typename traits::template rebind_alloc<node>;
        using node_traits = std::allocator_traits<node_allocator>;
        static_assert(std::is_nothrow_constructible_v<node_allocator, const Allocator&>);

        struct pending_node
        {
            node_allocator& allocation;
            node* memory;
            ~pending_node() noexcept
            {
                if (!memory) return;
                std::destroy_at(memory);
                if constexpr (default_heap) allocation.deallocate(memory, 1);
                else node_traits::deallocate(allocation, memory, 1);
            }
        };

        link header_;
        size_t size_ = 0;
        [[no_unique_address]] Compare compare_{};
        [[no_unique_address]] Allocator allocator_{};
        static node* as_node(link* p) noexcept { return static_cast<node*>(p); }
        static const Key& key(const link* p) noexcept { return static_cast<const node*>(p)->value.first; }
        static link* minimum(link* p) noexcept { while (p->left) p = p->left; return p; }
        static link* maximum(link* p) noexcept { while (p->right) p = p->right; return p; }

        static link* next(link* p) noexcept
        {
            if (p->right) return minimum(p->right);
            auto* parent = p->parent;
            while (parent->color != sentinel_color && p == parent->right) { p = parent; parent = parent->parent; }
            return parent;
        }

        static link* previous(link* p) noexcept
        {
            if (p->color == sentinel_color) return p->right;
            if (p->left) return maximum(p->left);
            auto* parent = p->parent;
            while (parent->color != sentinel_color && p == parent->left) { p = parent; parent = parent->parent; }
            return parent;
        }

        void reset_header() noexcept
        {
            header_.parent = nullptr;
            header_.left = header_.right = &header_;
            header_.color = sentinel_color;
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
            return top;
        }

        static bool is_red(const link* p) noexcept { return p && p->color == red; }
        static bool is_black(const link* p) noexcept { return !is_red(p); }

        void rebalance(link* p) noexcept
        {
            while (p->parent != &header_ && is_red(p->parent))
            {
                auto* parent = p->parent;
                auto* grand = parent->parent;
                if (parent == grand->left)
                {
                    auto* uncle = grand->right;
                    if (is_red(uncle)) { parent->color = uncle->color = black; grand->color = red; p = grand; }
                    else
                    {
                        if (p == parent->right) { p = parent; rotate_left(p); parent = p->parent; grand = parent->parent; }
                        parent->color = black; grand->color = red; rotate_right(grand);
                    }
                }
                else
                {
                    auto* uncle = grand->left;
                    if (is_red(uncle)) { parent->color = uncle->color = black; grand->color = red; p = grand; }
                    else
                    {
                        if (p == parent->left) { p = parent; rotate_right(p); parent = p->parent; grand = parent->parent; }
                        parent->color = black; grand->color = red; rotate_left(grand);
                    }
                }
            }
            header_.parent->color = black;
        }

        void erase_fixup(link* p, link* parent) noexcept
        {
            while (p != header_.parent && is_black(p))
            {
                if (p == parent->left)
                {
                    auto* sibling = parent->right;
                    if (is_red(sibling)) { sibling->color = black; parent->color = red; rotate_left(parent); sibling = parent->right; }
                    if (!sibling) { p = parent; parent = p->parent; continue; }
                    if (is_black(sibling->left) && is_black(sibling->right))
                    { sibling->color = red; p = parent; parent = p->parent; }
                    else
                    {
                        if (is_black(sibling->right))
                        {
                            if (sibling->left) sibling->left->color = black;
                            sibling->color = red; rotate_right(sibling); sibling = parent->right;
                        }
                        sibling->color = parent->color; parent->color = black;
                        if (sibling->right) sibling->right->color = black;
                        rotate_left(parent); p = header_.parent; parent = &header_;
                    }
                }
                else
                {
                    auto* sibling = parent->left;
                    if (is_red(sibling)) { sibling->color = black; parent->color = red; rotate_right(parent); sibling = parent->left; }
                    if (!sibling) { p = parent; parent = p->parent; continue; }
                    if (is_black(sibling->left) && is_black(sibling->right))
                    { sibling->color = red; p = parent; parent = p->parent; }
                    else
                    {
                        if (is_black(sibling->left))
                        {
                            if (sibling->right) sibling->right->color = black;
                            sibling->color = red; rotate_left(sibling); sibling = parent->left;
                        }
                        sibling->color = parent->color; parent->color = black;
                        if (sibling->left) sibling->left->color = black;
                        rotate_right(parent); p = header_.parent; parent = &header_;
                    }
                }
            }
            if (p) p->color = black;
        }

        template<class... Args>
        CE_ORDERED_MAP_INLINE node* make_node(Args&&... args)
        {
            static_assert(std::is_same_v<typename stored_type::first_type, const Key>);
            static_assert(std::is_same_v<typename stored_type::second_type, Mapped>);
            node_allocator allocation(allocator_);
            node* memory;
            if constexpr (default_heap) memory = allocation.allocate(1);
            else memory = node_traits::allocate(allocation, 1);
            std::construct_at(memory);
            pending_node pending{allocation, memory};
            if constexpr (default_heap) std::construct_at(std::addressof(memory->value), std::forward<Args>(args)...);
            else traits::construct(allocator_, std::addressof(memory->value), std::forward<Args>(args)...);
            pending.memory = nullptr;
            return memory;
        }

        template<class K, class... Args>
        CE_ORDERED_MAP_INLINE node* make_key_node(K&& keyValue, Args&&... args)
        {
            if constexpr (std::is_same_v<stored_type, std::pair<const Key, Mapped>>)
                return make_node(std::piecewise_construct, std::forward_as_tuple(std::forward<K>(keyValue)),
                                 std::forward_as_tuple(std::forward<Args>(args)...));
            else
                return make_node(ce::piecewise_construct,
                                 std::uses_allocator_construction_args<Key>(allocator_, std::forward<K>(keyValue)),
                                 std::uses_allocator_construction_args<Mapped>(allocator_, std::forward<Args>(args)...));
        }

        static void release_with(node* memory, Allocator& allocator) noexcept
        {
            if constexpr (default_heap) std::destroy_at(std::addressof(memory->value));
            else traits::destroy(allocator, std::addressof(memory->value));
            std::destroy_at(memory);
            node_allocator allocation(allocator);
            if constexpr (default_heap) allocation.deallocate(memory, 1);
            else node_traits::deallocate(allocation, memory, 1);
        }
        void release(link* p) noexcept { release_with(as_node(p), allocator_); }

        void destroy_branch(link* p) noexcept
        {
            if (!p) return;
            destroy_branch(p->left);
            destroy_branch(p->right);
            release(p);
        }

        struct position { link* parent; link* existing; bool left; };

        template<class Q> requires(lookup_key<Q>)
        position locate(const Q& value) const
        {
            auto* parent = const_cast<link*>(&header_);
            auto* current = header_.parent;
            link* candidate = nullptr;
            bool left = true;
            while (current)
            {
                parent = current;
                if constexpr (Multi) left = compare_(value, key(current));
                else left = !compare_(key(current), value);
                if (left) { if constexpr (!Multi) candidate = current; current = current->left; }
                else current = current->right;
            }
            if constexpr (!Multi)
                if (candidate && !compare_(value, key(candidate))) return {parent, candidate, false};
            return {parent, nullptr, left};
        }

        template<class Q> requires(lookup_key<Q>)
        position locate_transfer(const Q& value) const
        {
            if (!header_.parent) return {const_cast<link*>(&header_), nullptr, true};
            // Retire/admit streams often move ordered runs. Cached extrema
            // identify an unoccupied edge without another root traversal.
            if (compare_(value, key(header_.left))) return {header_.left, nullptr, true};
            if constexpr (Multi)
            {
                if (!compare_(value, key(header_.right))) return {header_.right, nullptr, false};
            }
            else if (compare_(key(header_.right), value)) return {header_.right, nullptr, false};
            return locate(value);
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
            if (where.parent == &header_) value->color = black;
            else { value->color = red; if (is_red(where.parent)) rebalance(value); }
        }

        template<class Q> requires(lookup_key<Q>)
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
        using mapped_type = Mapped;
        using value_type = stored_type;
        using allocator_type = Allocator;
        using key_compare = Compare;
        using size_type = size_t;

        template<bool Constant>
        class basic_iterator
        {
            friend class ordered_node_map;
            template<bool> friend class basic_iterator;
            link* position_ = nullptr;
            explicit basic_iterator(link* p) : position_(p) {}
        public:
            using value_type = stored_type;
            using difference_type = std::ptrdiff_t;
            using reference = std::conditional_t<Constant, const value_type&, value_type&>;
            using pointer = std::conditional_t<Constant, const value_type*, value_type*>;
            using iterator_category = std::bidirectional_iterator_tag;
            using iterator_concept = std::bidirectional_iterator_tag;
            basic_iterator() = default;
            template<bool C = Constant> requires(C)
            basic_iterator(const basic_iterator<false>& other) : position_(other.position_) {}
            reference operator*() const noexcept { return as_node(position_)->value; }
            pointer operator->() const noexcept { return std::addressof(as_node(position_)->value); }
            basic_iterator& operator++() noexcept { position_ = next(position_); return *this; }
            basic_iterator operator++(int) noexcept { auto old = *this; ++*this; return old; }
            basic_iterator& operator--() noexcept { position_ = previous(position_); return *this; }
            basic_iterator operator--(int) noexcept { auto old = *this; --*this; return old; }
            template<bool C> bool operator==(const basic_iterator<C>& other) const noexcept { return position_ == other.position_; }
        };
        using iterator = basic_iterator<false>;
        using const_iterator = basic_iterator<true>;
        using reverse_iterator = std::reverse_iterator<iterator>;
        using const_reverse_iterator = std::reverse_iterator<const_iterator>;

        class node_type
        {
            friend class ordered_node_map;
            std::optional<Allocator> allocator_;
            node* node_ = nullptr;
            node_type(node* p, const Allocator& allocator) : allocator_(allocator), node_(p) {}
            node* release_node() noexcept { allocator_.reset(); return std::exchange(node_, nullptr); }
        public:
            node_type() = default;
            node_type(const node_type&) = delete;
            node_type& operator=(const node_type&) = delete;
            node_type(node_type&& other) noexcept : allocator_(std::move(other.allocator_)), node_(std::exchange(other.node_, nullptr)) { other.allocator_.reset(); }
            node_type& operator=(node_type&& other) noexcept
            {
                if (this != &other) { reset(); allocator_ = std::move(other.allocator_); node_ = std::exchange(other.node_, nullptr); other.allocator_.reset(); }
                return *this;
            }
            ~node_type() { reset(); }
            void reset() noexcept { if (node_) release_with(node_, *allocator_); node_ = nullptr; allocator_.reset(); }
            bool empty() const noexcept { return !node_; }
            explicit operator bool() const noexcept { return node_ != nullptr; }
            const Key& key() const noexcept { return node_->value.first; }
            Mapped& mapped() const noexcept { return node_->value.second; }
            Allocator get_allocator() const noexcept { return *allocator_; }
        };
        struct insert_return_type { iterator position; bool inserted; node_type node; };

    private:
        bool compatible(const Allocator& other) const noexcept
        {
            if constexpr (traits::is_always_equal::value) return true;
            else return allocator_ == other;
        }
        void steal(ordered_node_map& other) noexcept
        {
            header_.parent = other.header_.parent;
            size_ = std::exchange(other.size_, 0);
            refresh_edges(); other.reset_header();
        }
        void swap_storage(ordered_node_map& other) noexcept
        {
            static_assert(std::is_nothrow_swappable_v<Compare>);
            using std::swap;
            swap(compare_, other.compare_); swap(header_.parent, other.header_.parent); swap(size_, other.size_);
            refresh_edges(); other.refresh_edges();
        }
        void swap_all(ordered_node_map& other) noexcept
        {
            static_assert(std::is_nothrow_swappable_v<Allocator>);
            using std::swap;
            swap(allocator_, other.allocator_);
            swap_storage(other);
        }

    public:
        ordered_node_map() { reset_header(); }
        explicit ordered_node_map(const Allocator& allocator) : allocator_(allocator) { reset_header(); }
        explicit ordered_node_map(const Compare& compare, const Allocator& allocator = Allocator()) : compare_(compare), allocator_(allocator) { reset_header(); }
        template<std::input_iterator It>
        ordered_node_map(It first, It last, const Compare& compare = Compare(), const Allocator& allocator = Allocator()) : ordered_node_map(compare, allocator)
        {
            try { insert(first, last); } catch (...) { clear(); throw; }
        }
        ordered_node_map(std::initializer_list<value_type> values, const Compare& compare = Compare(), const Allocator& allocator = Allocator())
            : ordered_node_map(values.begin(), values.end(), compare, allocator) {}
        template<std::input_iterator It>
        ordered_node_map(It first, It last, const Allocator& allocator) : ordered_node_map(first, last, Compare{}, allocator) {}
        ordered_node_map(std::initializer_list<value_type> values, const Allocator& allocator) : ordered_node_map(values, Compare{}, allocator) {}
        ordered_node_map(const ordered_node_map& other) : ordered_node_map(other, traits::select_on_container_copy_construction(other.allocator_)) {}
        ordered_node_map(const ordered_node_map& other, const Allocator& allocator) : ordered_node_map(other.begin(), other.end(), other.compare_, allocator) {}
        ordered_node_map(ordered_node_map&& other) noexcept(std::is_nothrow_move_constructible_v<Compare> && std::is_nothrow_move_constructible_v<Allocator>)
            : compare_(std::move(other.compare_)), allocator_(std::move(other.allocator_)) { reset_header(); steal(other); }
        ordered_node_map(ordered_node_map&& other, const Allocator& allocator) : ordered_node_map(other.compare_, allocator)
        {
            if (compatible(other.allocator_)) steal(other);
            else
            {
                try { for (auto& entry : other) try_emplace(entry.first, std::move(entry.second)); }
                catch (...) { clear(); throw; }
                other.clear();
            }
        }
        ~ordered_node_map() { clear(); }
        ordered_node_map& operator=(const ordered_node_map& other)
        {
            if (this != &other)
            {
                if constexpr (traits::propagate_on_container_copy_assignment::value) { ordered_node_map fresh(other, other.allocator_); swap_all(fresh); }
                else { ordered_node_map fresh(other, allocator_); swap_storage(fresh); }
            }
            return *this;
        }
        ordered_node_map& operator=(ordered_node_map&& other)
        {
            if (this != &other)
            {
                if constexpr (traits::propagate_on_container_move_assignment::value) { ordered_node_map fresh(std::move(other)); swap_all(fresh); }
                else { ordered_node_map fresh(std::move(other), allocator_); swap_storage(fresh); }
            }
            return *this;
        }
        ordered_node_map& operator=(std::initializer_list<value_type> values) { ordered_node_map fresh(values, compare_, allocator_); swap_storage(fresh); return *this; }
        void swap(ordered_node_map& other) noexcept(traits::propagate_on_container_swap::value || traits::is_always_equal::value)
        {
            if (this == &other) return;
            if constexpr (traits::propagate_on_container_swap::value) swap_all(other);
            else
            {
                if constexpr (!traits::is_always_equal::value) if (!compatible(other.allocator_)) throw std::invalid_argument("ordered map allocator mismatch");
                swap_storage(other);
            }
        }
        void clear() noexcept { destroy_branch(header_.parent); size_ = 0; reset_header(); }
        bool empty() const noexcept { return size_ == 0; }
        size_t size() const noexcept { return size_; }
        Allocator get_allocator() const noexcept { return allocator_; }
        Compare key_comp() const { return compare_; }
        iterator begin() noexcept { return iterator(header_.left); }
        iterator end() noexcept { return iterator(&header_); }
        const_iterator begin() const noexcept { return const_iterator(header_.left); }
        const_iterator end() const noexcept { return const_iterator(const_cast<link*>(&header_)); }
        const_iterator cbegin() const noexcept { return begin(); }
        const_iterator cend() const noexcept { return end(); }
        reverse_iterator rbegin() noexcept { return reverse_iterator(end()); }
        reverse_iterator rend() noexcept { return reverse_iterator(begin()); }
        const_reverse_iterator rbegin() const noexcept { return const_reverse_iterator(end()); }
        const_reverse_iterator rend() const noexcept { return const_reverse_iterator(begin()); }

        template<class Q> requires(lookup_key<Q>) iterator lower_bound(const Q& value) { return iterator(bound(value, false)); }
        template<class Q> requires(lookup_key<Q>) const_iterator lower_bound(const Q& value) const { return const_iterator(bound(value, false)); }
        template<class Q> requires(lookup_key<Q>) iterator upper_bound(const Q& value) { return iterator(bound(value, true)); }
        template<class Q> requires(lookup_key<Q>) const_iterator upper_bound(const Q& value) const { return const_iterator(bound(value, true)); }
        template<class Q> requires(lookup_key<Q>) ce::pair<iterator, iterator> equal_range(const Q& value) { return {lower_bound(value), upper_bound(value)}; }
        template<class Q> requires(lookup_key<Q>) ce::pair<const_iterator, const_iterator> equal_range(const Q& value) const { return {lower_bound(value), upper_bound(value)}; }
        template<class Q> requires(lookup_key<Q>) iterator find(const Q& value) { auto p = lower_bound(value); return p != end() && !compare_(value, p->first) ? p : end(); }
        template<class Q> requires(lookup_key<Q>) const_iterator find(const Q& value) const { auto p = lower_bound(value); return p != end() && !compare_(value, p->first) ? p : end(); }
        template<class Q> requires(lookup_key<Q>) bool contains(const Q& value) const { return find(value) != end(); }
        template<class Q> requires(lookup_key<Q>) size_t count(const Q& value) const
        {
            if constexpr (!Multi) return contains(value) ? 1 : 0;
            else { auto range = equal_range(value); return static_cast<size_t>(std::distance(range.first, range.second)); }
        }
        template<class Q> requires(lookup_key<Q>) Mapped& at(const Q& value) { auto p = find(value); if (p == end()) throw std::out_of_range("ordered map::at"); return p->second; }
        template<class Q> requires(lookup_key<Q>) const Mapped& at(const Q& value) const { auto p = find(value); if (p == end()) throw std::out_of_range("ordered map::at"); return p->second; }

        iterator find(const Key& value) { return find<Key>(value); }
        const_iterator find(const Key& value) const { return find<Key>(value); }
        bool contains(const Key& value) const { return contains<Key>(value); }
        size_t count(const Key& value) const { return count<Key>(value); }
        iterator lower_bound(const Key& value) { return lower_bound<Key>(value); }
        const_iterator lower_bound(const Key& value) const { return lower_bound<Key>(value); }
        iterator upper_bound(const Key& value) { return upper_bound<Key>(value); }
        const_iterator upper_bound(const Key& value) const { return upper_bound<Key>(value); }
        ce::pair<iterator, iterator> equal_range(const Key& value) { return equal_range<Key>(value); }
        ce::pair<const_iterator, const_iterator> equal_range(const Key& value) const { return equal_range<Key>(value); }
        Mapped& at(const Key& value) { return at<Key>(value); }
        const Mapped& at(const Key& value) const { return at<Key>(value); }
    private:
        template<class K, class... Args> CE_ORDERED_MAP_INLINE auto try_emplace_key(K&& value, Args&&... args)
        {
            auto where = locate(value);
            if constexpr (!Multi)
            {
                if (where.existing) return ce::pair<iterator, bool>{iterator(where.existing), false};
                auto* fresh = make_key_node(std::forward<K>(value), std::forward<Args>(args)...); attach(fresh, where);
                return ce::pair<iterator, bool>{iterator(fresh), true};
            }
            else { auto* fresh = make_key_node(std::forward<K>(value), std::forward<Args>(args)...); attach(fresh, where); return iterator(fresh); }
        }
    public:
        template<class... Args> auto try_emplace(const Key& value, Args&&... args) { return try_emplace_key(value, std::forward<Args>(args)...); }
        template<class... Args> auto try_emplace(Key&& value, Args&&... args) { return try_emplace_key(std::move(value), std::forward<Args>(args)...); }
        template<class K, class... Args> requires (!std::is_same_v<std::remove_cvref_t<K>, Key>)
        auto try_emplace(K&& value, Args&&... args) { return try_emplace_key(Key(std::forward<K>(value)), std::forward<Args>(args)...); }
        Mapped& operator[](const Key& value) requires (!Multi) { return try_emplace(value).first->second; }
        Mapped& operator[](Key&& value) requires (!Multi) { return try_emplace(std::move(value)).first->second; }
    private:
        template<class K, class V> auto insert_or_assign_key(K&& value, V&& mapped) requires (!Multi)
        {
            auto p = find(value);
            if (p != end()) { p->second = std::forward<V>(mapped); return ce::pair<iterator, bool>{p, false}; }
            return try_emplace(std::forward<K>(value), std::forward<V>(mapped));
        }
    public:
        template<class V> auto insert_or_assign(const Key& value, V&& mapped) requires (!Multi) { return insert_or_assign_key(value, std::forward<V>(mapped)); }
        template<class V> auto insert_or_assign(Key&& value, V&& mapped) requires (!Multi) { return insert_or_assign_key(std::move(value), std::forward<V>(mapped)); }
        template<class K, class V> requires (!std::is_same_v<std::remove_cvref_t<K>, Key> && !Multi)
        auto insert_or_assign(K&& value, V&& mapped) { return insert_or_assign_key(Key(std::forward<K>(value)), std::forward<V>(mapped)); }
        template<class P> auto insert_pair(P&& value) { return try_emplace(std::forward<P>(value).first, std::forward<P>(value).second); }
        auto insert(const value_type& value) { return insert_pair(value); }
        auto insert(value_type&& value) { return insert_pair(std::move(value)); }
        template<class P> requires requires(P&& p) { p.first; p.second; }
        auto insert(P&& value) { return insert_pair(std::forward<P>(value)); }
        template<std::input_iterator It> void insert(It first, It last) { for (; first != last; ++first) insert(*first); }
        void insert(std::initializer_list<value_type> values) { insert(values.begin(), values.end()); }
        template<class... Args> auto emplace(Args&&... args)
        {
            auto* fresh = make_node(std::forward<Args>(args)...);
            position where;
            try { where = locate(fresh->value.first); } catch (...) { release(fresh); throw; }
            if constexpr (!Multi)
            {
                if (where.existing) { release(fresh); return ce::pair<iterator, bool>{iterator(where.existing), false}; }
                attach(fresh, where); return ce::pair<iterator, bool>{iterator(fresh), true};
            }
            else { attach(fresh, where); return iterator(fresh); }
        }
        template<class... Args> iterator emplace_hint(const_iterator, Args&&... args)
        {
            if constexpr (Multi) return emplace(std::forward<Args>(args)...);
            else return emplace(std::forward<Args>(args)...).first;
        }
        iterator insert(const_iterator hint, const value_type& value) { return emplace_hint(hint, value); }
        iterator insert(const_iterator hint, value_type&& value) { return emplace_hint(hint, std::move(value)); }

    private:
        static int audit_branch(const link* p, const link* parent, size_t& nodes) noexcept
        {
            if (!p) return 1;
            if (p->parent != parent || (p->color != black && p->color != red)) return -1;
            if (is_red(p) && (is_red(p->left) || is_red(p->right))) return -1;
            ++nodes;
            const int left = audit_branch(p->left, p, nodes), right = audit_branch(p->right, p, nodes);
            if (left < 0 || left != right) return -1;
            return left + (p->color == black ? 1 : 0);
        }

    public:
        bool validate_structure() const noexcept
        {
            if (!header_.parent) return size_ == 0 && header_.left == &header_ && header_.right == &header_;
            if (header_.parent->color != black || header_.left != minimum(header_.parent) || header_.right != maximum(header_.parent)) return false;
            size_t nodes = 0;
            return audit_branch(header_.parent, &header_, nodes) > 0 && nodes == size_;
        }

    private:
        CE_ORDERED_MAP_INLINE link* unlink(link* erased) noexcept
        {
            auto* result = next(erased);
            if (erased == header_.left) header_.left = result;
            if (erased == header_.right) header_.right = previous(erased);
            auto* replacement = erased;
            int removedColor = replacement->color;
            link* child;
            link* parent;
            if (!erased->left)
            {
                child = erased->right; parent = erased->parent; replace(erased, child);
            }
            else if (!erased->right)
            {
                child = erased->left; parent = erased->parent; replace(erased, child);
            }
            else
            {
                replacement = minimum(erased->right);
                removedColor = replacement->color;
                child = replacement->right;
                if (replacement->parent == erased)
                {
                    parent = replacement;
                    if (child) child->parent = replacement;
                }
                else
                {
                    parent = replacement->parent;
                    replace(replacement, child);
                    replacement->right = erased->right; replacement->right->parent = replacement;
                }
                replace(erased, replacement);
                replacement->left = erased->left; replacement->left->parent = replacement;
                replacement->color = erased->color;
            }
            --size_;
            if (removedColor == black)
            {
                if (parent == &header_) { if (child) child->color = black; }
                else erase_fixup(child, parent);
            }
            if (!size_) reset_header();
            return result;
        }


    public:
        iterator erase(const_iterator position) noexcept { auto* p = position.position_; auto* result = unlink(p); release(p); return iterator(result); }
        iterator erase(iterator position) noexcept { return erase(const_iterator(position)); }
        iterator erase(const_iterator first, const_iterator last) noexcept { while (first != last) first = erase(first); return iterator(first.position_); }
        template<class Q> requires(lookup_key<Q>) size_t erase(const Q& value)
        {
            auto range = equal_range(value); size_t removed = 0;
            while (range.first != range.second) { range.first = erase(range.first); ++removed; }
            return removed;
        }
        node_type extract(const_iterator position)
        {
            auto* p = position.position_;
            node_type result(as_node(p), allocator_);
            unlink(p);
            p->parent = p->left = p->right = nullptr; p->color = black;
            return result;
        }
        node_type extract(iterator position) { return extract(const_iterator(position)); }
        template<class Q> requires(lookup_key<Q>) node_type extract(const Q& value) { auto p = find(value); return p == end() ? node_type{} : extract(p); }
        auto insert(node_type&& value)
        {
            if constexpr (!Multi)
            {
                if (!value) return insert_return_type{end(), false, {}};
                if (!compatible(*value.allocator_)) throw std::invalid_argument("ordered map node allocator mismatch");
                auto where = locate_transfer(value.key());
                if (where.existing) return insert_return_type{iterator(where.existing), false, std::move(value)};
                auto* p = value.release_node(); attach(p, where);
                return insert_return_type{iterator(p), true, {}};
            }
            else
            {
                if (!value) return end();
                if (!compatible(*value.allocator_)) throw std::invalid_argument("ordered map node allocator mismatch");
                auto where = locate_transfer(value.key()); auto* p = value.release_node(); attach(p, where); return iterator(p);
            }
        }
        void merge(ordered_node_map& source)
        {
            if (this == &source) return;
            if (!compatible(source.allocator_)) throw std::invalid_argument("ordered map merge allocator mismatch");
            for (auto p = source.begin(); p != source.end();)
            {
                auto current = p++;
                auto where = locate(current->first);
                if (!where.existing)
                {
                    auto* n = current.position_; source.unlink(n);
                    n->parent = n->left = n->right = nullptr; n->color = black;
                    attach(as_node(n), where);
                }
            }
        }
        friend void swap(ordered_node_map& a, ordered_node_map& b) noexcept(noexcept(a.swap(b))) { a.swap(b); }
        friend bool operator==(const ordered_node_map& a, const ordered_node_map& b)
        {
            return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin());
        }
    };
#undef CE_ORDERED_MAP_INLINE
}
