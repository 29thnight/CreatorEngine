#pragma once
// Canonical runtime value adapters. Include this from ReflgenRegistration.h so
// every generated module registration sees the same value descriptors. Native
// ownership and GC identities are borrowed, never reconstructed from JSON.
#include "ReflectionTypedYml.h"
#include "ScriptReflectionPrimitives.h"
#include "ScriptReflectionContainers.h"
#include "ScriptReflectionOwnership.h"
#include "ScriptReflectionMath.h"
#include <reflgen/runtime/value_descriptor.h>
#include <reflgen/runtime/schema.h>

namespace reflgen
{
    // Authoring record serializers call lifecycle/property hooks and write
    // persistence envelopes. Runtime calls use direct fields while retaining
    // the same recursive capability checks and readonly policy.
    template<class T> requires Meta::Typed::ReflgenRecord<T>
    struct runtime_value_traits<T>
    {
        static constexpr bool prefer_record_fields = true;
    };

    template<> struct runtime_value_traits<FileGuid>
    {
        static constexpr value_shape shape = value_shape::string;
        static constexpr bool assignment_safe = true;
        using children = type_list<>;
    };

    template<> struct runtime_value_traits<HashedGuid>
    {
        static constexpr value_shape shape = value_shape::unsigned_integer;
        static constexpr bool assignment_safe = true;
        using children = type_list<>;
    };

    template<class T> struct runtime_value_traits<gc::trace_ref<T>>
    {
        static constexpr value_shape shape = value_shape::pointer;
        static constexpr bool mutable_borrow = !std::is_const_v<T>;
        static constexpr bool serializable = false;
        static constexpr bool deserializable = false;
        static constexpr bool assignment_safe = false;
        using children = type_list<T>;
        static const T* get(const gc::trace_ref<T>& value) noexcept { return value.get(); }
        static T* get(gc::trace_ref<T>& value) noexcept { return value.get(); }
    };

    template<class T> struct runtime_value_traits<gc::root_ref<T>>
    {
        static constexpr value_shape shape = value_shape::pointer;
        static constexpr bool mutable_borrow = !std::is_const_v<T>;
        static constexpr bool retained_borrow = true;
        static constexpr bool serializable = false;
        static constexpr bool deserializable = false;
        static constexpr bool assignment_safe = false;
        using children = type_list<T>;
        static const T* get(const gc::root_ref<T>& value) noexcept { return value.get(); }
        static T* get(gc::root_ref<T>& value) noexcept { return value.get(); }
    };

    // Legacy authored graphs still contain raw record references (animation
    // parameters/masks). Their current pointer is re-read under the component's
    // owner-thread root on every operation. No pointer survives the request:
    // retained_borrow only keeps the returned pointer value until that request
    // finishes resolving its path. The component's native borrow contract must
    // keep that record alive during the call. Neither addresses nor ownership
    // are exposed to managed callers, and a pointer field cannot be rebound.
    template<class T> requires std::is_class_v<std::remove_cv_t<T>>
    struct runtime_value_traits<T*>
    {
        static constexpr value_shape shape = value_shape::pointer;
        static constexpr bool mutable_borrow = !std::is_const_v<T>;
        static constexpr bool retained_borrow = true;
        static constexpr bool serializable = false;
        static constexpr bool deserializable = false;
        static constexpr bool assignment_safe = false;
        using children = type_list<T>;
        static const T* get(T* const& value) noexcept { return value; }
        static T* get(T*& value) noexcept { return value; }
    };

}
