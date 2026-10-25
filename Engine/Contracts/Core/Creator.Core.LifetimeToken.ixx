export module Creator.Core.LifetimeToken;

import std;
import Creator.Core.Ownership;

export namespace ce
{
    // Type erasure stores the real own handle in place. No payload or control
    // block allocation; strong and weak releases remain ownership_cpp operations.
    class lifetime_token;
    class weak_lifetime_token;
    namespace lifetime_detail
    {
        struct operations
        {
            void (*copy)(void*, const void*);
            void (*move)(void*, void*);
            void (*destroy)(void*);
            void (*weaken)(void*, const void*);
            void (*weak_copy)(void*, const void*);
            void (*weak_move)(void*, void*);
            void (*weak_destroy)(void*);
            bool (*expired)(const void*);
            bool (*lock)(void*, const void*);
            const void* (*address)(const void*);
            std::size_t (*count)(const void*);
        };
        template<class T>
        const operations& ops_for()
        {
            using strong = own::shared_owner<T>;
            using weak = own::weak_owner<T>;
            static_assert(sizeof(strong) <= 2 * sizeof(void*) && sizeof(weak) <= 2 * sizeof(void*));
            static const operations table{
                [](void* a, const void* b)
                { std::construct_at(static_cast<strong*>(a), *static_cast<const strong*>(b)); },
                [](void* a, void* b)
                { std::construct_at(static_cast<strong*>(a), std::move(*static_cast<strong*>(b))); },
                [](void* a) { std::destroy_at(static_cast<strong*>(a)); },
                [](void* a, const void* b)
                { std::construct_at(static_cast<weak*>(a), *static_cast<const strong*>(b)); },
                [](void* a, const void* b) { std::construct_at(static_cast<weak*>(a), *static_cast<const weak*>(b)); },
                [](void* a, void* b) { std::construct_at(static_cast<weak*>(a), std::move(*static_cast<weak*>(b))); },
                [](void* a) { std::destroy_at(static_cast<weak*>(a)); },
                [](const void* a) { return static_cast<const weak*>(a)->expired(); },
                [](void* a, const void* b)
                {
                    auto value = static_cast<const weak*>(b)->lock();
                    const bool valid = bool(value);
                    std::construct_at(static_cast<strong*>(a), std::move(value));
                    return valid;
                },
                [](const void* a) -> const void*
                {
                    const auto& value = *static_cast<const strong*>(a);
                    return value ? value.operator->() : nullptr;
                },
                [](const void* a) { return static_cast<const strong*>(a)->use_count(); }};
            return table;
        }
    } // namespace lifetime_detail
    class lifetime_token
    {
      public:
        lifetime_token() = default;
        lifetime_token(std::nullptr_t) {}
        template<class T>
        lifetime_token(const own::shared_owner<T>& owner)
        {
            if (owner)
            {
                ops_ = &lifetime_detail::ops_for<T>();
                std::construct_at(reinterpret_cast<own::shared_owner<T>*>(storage_), owner);
            }
        }
        template<class T>
        lifetime_token(own::shared_owner<T>&& owner)
        {
            if (owner)
            {
                ops_ = &lifetime_detail::ops_for<T>();
                std::construct_at(reinterpret_cast<own::shared_owner<T>*>(storage_), std::move(owner));
            }
        }
        lifetime_token(const lifetime_token& other) : ops_(other.ops_)
        {
            if (ops_)
            {
                ops_->copy(storage_, other.storage_);
            }
        }
        lifetime_token(lifetime_token&& other) noexcept : ops_(other.ops_)
        {
            if (ops_)
            {
                ops_->move(storage_, other.storage_);
            }
        }
        ~lifetime_token() { reset(); }
        lifetime_token& operator=(const lifetime_token& other)
        {
            if (this != &other)
            {
                reset();
                ops_ = other.ops_;
                if (ops_)
                {
                    ops_->copy(storage_, other.storage_);
                }
            }
            return *this;
        }
        lifetime_token& operator=(lifetime_token&& other) noexcept
        {
            if (this != &other)
            {
                reset();
                ops_ = other.ops_;
                if (ops_)
                {
                    ops_->move(storage_, other.storage_);
                }
            }
            return *this;
        }
        void reset() noexcept
        {
            if (ops_)
            {
                ops_->destroy(storage_);
                ops_ = nullptr;
            }
        }
        explicit operator bool() const { return unsafe_get() != nullptr; }
        const void* operator->() const { return unsafe_get(); }
        const void* unsafe_get() const { return ops_ ? ops_->address(storage_) : nullptr; }
        std::size_t use_count() const { return ops_ ? ops_->count(storage_) : 0; }
        friend bool operator==(const lifetime_token& value, std::nullptr_t) { return !value; }

      private:
        alignas(void*) std::byte storage_[2 * sizeof(void*)]{};
        const lifetime_detail::operations* ops_{};
        friend class weak_lifetime_token;
    };
    class weak_lifetime_token
    {
      public:
        weak_lifetime_token() = default;
        template<class T>
        weak_lifetime_token(const own::shared_owner<T>& owner)
        {
            if (owner)
            {
                ops_ = &lifetime_detail::ops_for<T>();
                ops_->weaken(storage_, &owner);
            }
        }
        weak_lifetime_token(const lifetime_token& owner) : ops_(owner.ops_)
        {
            if (ops_)
            {
                ops_->weaken(storage_, owner.storage_);
            }
        }
        weak_lifetime_token(const weak_lifetime_token& other) : ops_(other.ops_)
        {
            if (ops_)
            {
                ops_->weak_copy(storage_, other.storage_);
            }
        }
        weak_lifetime_token(weak_lifetime_token&& other) noexcept : ops_(other.ops_)
        {
            if (ops_)
            {
                ops_->weak_move(storage_, other.storage_);
            }
        }
        ~weak_lifetime_token() { reset(); }
        weak_lifetime_token& operator=(const weak_lifetime_token& other)
        {
            if (this != &other)
            {
                reset();
                ops_ = other.ops_;
                if (ops_)
                {
                    ops_->weak_copy(storage_, other.storage_);
                }
            }
            return *this;
        }
        weak_lifetime_token& operator=(weak_lifetime_token&& other) noexcept
        {
            if (this != &other)
            {
                reset();
                ops_ = other.ops_;
                if (ops_)
                {
                    ops_->weak_move(storage_, other.storage_);
                }
            }
            return *this;
        }
        void reset() noexcept
        {
            if (ops_)
            {
                ops_->weak_destroy(storage_);
                ops_ = nullptr;
            }
        }
        bool expired() const { return !ops_ || ops_->expired(storage_); }
        lifetime_token lock() const
        {
            lifetime_token value;
            if (ops_)
            {
                value.ops_ = ops_;
                ops_->lock(value.storage_, storage_);
            }
            return value;
        }

      private:
        alignas(void*) std::byte storage_[2 * sizeof(void*)]{};
        const lifetime_detail::operations* ops_{};
    };
} // namespace ce
