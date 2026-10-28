#pragma once
// Explicit owner borrows retain native ownership; JSON cannot replace an owner.
#include "../../Contracts/External/Creator.Ownership.h"
#include <reflgen/runtime/value_descriptor.h>
#include <memory>

namespace reflgen
{
    template<class T> struct runtime_value_traits<std::shared_ptr<T>>
    {
        static constexpr value_shape shape = value_shape::pointer;
        static constexpr bool mutable_borrow = !std::is_const_v<T>;
        static constexpr bool retained_borrow = true;
        static constexpr bool serializable = false;
        static constexpr bool deserializable = false;
        static constexpr bool assignment_safe = false;
        using children = type_list<T>;
        static const T* get(const std::shared_ptr<T>& value) noexcept { return value.get(); }
        static T* get(std::shared_ptr<T>& value) noexcept { return value.get(); }
    };

    template<class T, class Deleter> struct runtime_value_traits<std::unique_ptr<T, Deleter>>
    {
        static constexpr value_shape shape = value_shape::pointer;
        static constexpr bool mutable_borrow = !std::is_const_v<T>;
        static constexpr bool retained_borrow = true;
        static constexpr bool serializable = false;
        static constexpr bool deserializable = false;
        static constexpr bool assignment_safe = false;
        using children = type_list<T>;
        static const T* get(const std::unique_ptr<T, Deleter>& value) noexcept { return value.get(); }
        static T* get(std::unique_ptr<T, Deleter>& value) noexcept { return value.get(); }
    };

    template<class T> struct runtime_value_traits<own::shared_owner<T>>
    {
        static constexpr value_shape shape = value_shape::pointer;
        static constexpr bool mutable_borrow = !std::is_const_v<T>;
        static constexpr bool retained_borrow = true;
        static constexpr bool serializable = false;
        static constexpr bool deserializable = false;
        static constexpr bool assignment_safe = false;
        using children = type_list<T>;
        static const T* get(const own::shared_owner<T>& value) noexcept { return value.borrow().unsafe_get(); }
        static T* get(own::shared_owner<T>& value) noexcept { return value.borrow().unsafe_get(); }
    };

    template<class T> struct runtime_value_traits<own::unique_owner<T>>
    {
        static constexpr value_shape shape = value_shape::pointer;
        static constexpr bool mutable_borrow = !std::is_const_v<T>;
        static constexpr bool retained_borrow = true;
        static constexpr bool serializable = false;
        static constexpr bool deserializable = false;
        static constexpr bool assignment_safe = false;
        using children = type_list<T>;
        static const T* get(const own::unique_owner<T>& value) noexcept { return value.borrow().unsafe_get(); }
        static T* get(own::unique_owner<T>& value) noexcept { return value.borrow().unsafe_get(); }
    };
}
