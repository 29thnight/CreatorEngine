#pragma once
#if defined(CE_CONTRACT_STANDALONE)
#include "../../Tools/build/ModuleAdapters/Creator.Core.LifetimeToken.h"
#else
import Creator.Core.LifetimeToken;
#endif
#if defined(CE_CONTRACT_STANDALONE)
#include "../../Tools/build/ModuleAdapters/Creator.Core.HeapAllocator.h"
#else
import Creator.Core.HeapAllocator;
#endif
#if defined(CE_CONTRACT_STANDALONE)
#include "../../Tools/build/ModuleAdapters/Creator.Core.Ownership.h"
#else
import Creator.Core.Ownership;
#endif
#include <limits>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>
#include <vector>

// Renderer-owned CPU storage only. Cross-thread destruction uses mi_free;
// no private heap is destroyed while a recording or GPU retirement owns data.
namespace ce
{
    template<typename T>
    using render_allocator = heap_allocator<T>;

    template<typename T>
    using render_vector = std::vector<T, render_allocator<T>>;

    template<typename T>
    struct render_delete
    {
        void operator()(T* value) const noexcept
        {
            if (value)
            {
                std::destroy_at(value);
                render_allocator<T>{}.deallocate(value, 1);
            }
        }
    };

    struct render_object_allocator : own::allocator_interface<render_object_allocator>
    {
        void* do_allocate(size_t size, size_t alignment) { return ce::heap::try_allocate_aligned(size, alignment); }
        void do_deallocate(void* pointer, size_t, size_t) noexcept { ce::heap::deallocate(pointer); }
    };
    inline own::allocator_ref render_owner_allocator()
    {
        return {nullptr,
                [](void*, size_t size, size_t alignment) -> void* { return ce::heap::try_allocate_aligned(size, alignment); },
                [](void*, void* pointer, size_t, size_t) noexcept { ce::heap::deallocate(pointer); }};
    }
    template<typename T>
    using render_unique_ptr = own::unique_owner<T, render_object_allocator>;
    template<typename T, typename... Args>
    render_unique_ptr<T> make_render_unique(Args&&... args)
    {
        return render_unique_ptr<T>(std::in_place, std::forward<Args>(args)...);
    }
    template<typename T, typename... Args>
    own::shared_owner<T> make_render_shared(Args&&... args)
    {
        return own::allocate_shared<T>(render_owner_allocator(), std::forward<Args>(args)...);
    }
    namespace pool_detail
    {
        template<class T>
        struct lease
        {
            explicit lease(T* value) : pointer_(value) {}
            virtual ~lease() = default;
            T* pointer_;
        };
        template<class T, class Return>
        struct returned_lease final : lease<T>
        {
            returned_lease(T* value, const Return& release) : lease<T>(value), callback_(release) {}
            ~returned_lease() override { callback_(this->pointer_); }
            Return callback_;
        };
    } // namespace pool_detail
    template<class T>
    class weak_pooled_owner;
    // Inline lifetime token permits both factory-owned and pool-returned
    // polymorphic RHI objects without a second reference-count control block.
    template<class T>
    class pooled_owner
    {
      public:
        pooled_owner() = default;
        pooled_owner(std::nullptr_t) {}
        template<class U>
            requires std::is_convertible_v<U*, T*>
        pooled_owner(const own::shared_owner<U>& owner) : pointer_(owner.operator->()), owner_(owner)
        {
        }
        template<class U>
            requires std::is_convertible_v<U*, T*>
        pooled_owner(own::shared_owner<U>&& owner) : pointer_(owner.operator->()), owner_(std::move(owner))
        {
        }
        pooled_owner(const pooled_owner&) = default;
        pooled_owner(pooled_owner&& other) noexcept
            : pointer_(std::exchange(other.pointer_, nullptr)), owner_(std::move(other.owner_))
        {
        }
        pooled_owner& operator=(const pooled_owner&) = default;
        pooled_owner& operator=(pooled_owner&& other) noexcept
        {
            if (this != &other)
            {
                owner_ = std::move(other.owner_);
                pointer_ = std::exchange(other.pointer_, nullptr);
            }
            return *this;
        }
        template<class U>
            requires std::is_convertible_v<U*, T*>
        pooled_owner(const pooled_owner<U>& other) : pointer_(other.pointer_), owner_(other.owner_)
        {
        }
        template<class Return, class Allocator>
        pooled_owner(T* value, Return callback, Allocator)
        {
            try
            {
                owner_ = make_render_shared<pool_detail::returned_lease<T, Return>>(value, callback);
                pointer_ = value;
            }
            catch (...)
            {
                callback(value);
                throw;
            }
        }
        template<class U>
        pooled_owner<U> dynamic_cast_to() const
        {
            auto* pointer = dynamic_cast<U*>(pointer_);
            return pointer ? pooled_owner<U>(pointer, owner_) : pooled_owner<U>{};
        }
        T* operator->() const { return pointer_; }
        T& operator*() const { return *pointer_; }
        explicit operator bool() const { return bool(owner_); }
        operator lifetime_token() const { return owner_; }
        void reset()
        {
            owner_.reset();
            pointer_ = nullptr;
        }
        size_t use_count() const { return owner_.use_count(); }
        friend bool operator==(const pooled_owner& value, std::nullptr_t) { return !value; }

      private:
        pooled_owner(T* pointer, lifetime_token owner) : pointer_(owner ? pointer : nullptr), owner_(std::move(owner))
        {
        }
        T* pointer_{};
        lifetime_token owner_;
        template<class U>
        friend class pooled_owner;
        friend class weak_pooled_owner<T>;
    };
    template<class T>
    class weak_pooled_owner
    {
      public:
        weak_pooled_owner() = default;
        weak_pooled_owner(const pooled_owner<T>& value) : pointer_(value.pointer_), owner_(value.owner_) {}
        pooled_owner<T> lock() const { return pooled_owner<T>(pointer_, owner_.lock()); }
        bool expired() const { return owner_.expired(); }
        void reset()
        {
            owner_.reset();
            pointer_ = nullptr;
        }

      private:
        T* pointer_{};
        weak_lifetime_token owner_;
    };
    template<class To, class From>
    pooled_owner<To> dynamic_pooled_cast(const pooled_owner<From>& source)
    {
        return source.template dynamic_cast_to<To>();
    }
} // namespace ce
