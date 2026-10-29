#pragma once
// Canonical adapters for strongly typed engine IDs. Template instantiations are
// described here once rather than generating a separate schema in each owner.
#include "Experiment/ModelData.h"
#include "../Physics/PhysicsTypes.h"
#include "AssetDepot/AssetLink.h"
#include "RHI/RHIHandle.h"
#include "../Utility_Framework/Foundation/TypeDefinition.h"
#if defined(CE_CONTRACT_STANDALONE)
#include "../../Tools/build/ModuleAdapters/Creator.Core.Layers.h"
#include "../../Tools/build/ModuleAdapters/Creator.Core.Delegate.h"
#else
import Creator.Core.Layers;
import Creator.Core.Delegate;
#endif
#include <reflgen/runtime/schema.h>
#include <reflgen/runtime/value_descriptor.h>

namespace reflgen
{
    // An RHI handle is issued by a live backend table. Its numeric identity can
    // be observed and an existing native handle can be borrowed, but JSON does
    // not establish that a resource is alive or owned by the active device.
    template<class T> requires (std::is_same_v<T, RHITextureHandle> || std::is_same_v<T, RHIBufferHandle>)
    struct runtime_value_traits<T>
    {
        static constexpr bool deserializable = false;
        static constexpr bool assignment_safe = false;
    };

    template<> struct runtime_schema<Core::Sizef>
    {
        using T = Core::Sizef;
        static constexpr auto value = schema<T>(field<&T::width>, field<&T::height>);
    };

    template<> struct runtime_schema<ce::layers::layer_id>
    {
        using T = ce::layers::layer_id;
        static constexpr auto value = schema<T>(field<&T::value>);
    };

    // Delegate IDs are observations of subscriptions owned by the engine. A
    // number must not be accepted as authority to forge or replace a handle.
    template<> struct runtime_value_traits<ce::delegate_handle>
    {
        static constexpr value_shape shape = value_shape::unsigned_integer;
        static constexpr bool deserializable = false;
        static constexpr bool assignment_safe = false;
        using children = type_list<>;
        static void write(writer& out, const ce::delegate_handle& value)
        {
            out.write_uint(value.id());
        }
    };

    template<class Value> struct runtime_schema<AssetDepot::AssetLink<Value>>
    {
        using T = AssetDepot::AssetLink<Value>;
        static constexpr auto value = schema<T>(field<&T::identity>, method<&T::IsValid>, method<&T::ToReference>);
    };

    template<> struct runtime_value_traits<uuid::uuid16>
    {
        static constexpr value_shape shape = value_shape::string;
        static constexpr bool assignment_safe = true;
        using children = type_list<>;
        static void write(writer& out, const uuid::uuid16& value) { out.write_string(uuid::to_string(value)); }
        static void read(reader& in, uuid::uuid16& value) { value = uuid::parse(in.read_string()); }
    };

    template<class Tag> struct runtime_value_traits<experiment::Index<Tag>>
    {
        static constexpr value_shape shape = value_shape::unsigned_integer;
        static constexpr bool assignment_safe = true;
        using children = type_list<>;
        static void write(writer& out, const experiment::Index<Tag>& value) { out.write_uint(value.Value()); }
        static void read(reader& in, experiment::Index<Tag>& value)
        {
            const auto number = in.read_uint();
            if (number > (std::numeric_limits<std::uint32_t>::max)())
                throw serialization_error("native model index exceeds uint32");
            value = experiment::Index<Tag>(static_cast<std::uint32_t>(number));
        }
    };

    template<class Tag> struct runtime_schema<experiment::Index<Tag>>
    {
        using T = experiment::Index<Tag>;
        static constexpr auto value = schema<T>(method<&T::Value>, method<&T::IsValid>);
    };

    template<class Tag> struct runtime_schema<ce::physics::handle<Tag>>
    {
        using T = ce::physics::handle<Tag>;
        static constexpr auto value = schema<T>(field<&T::scene>, field<&T::slot>, field<&T::generation>);
    };
}
