#pragma once
// Canonical adapters for strongly typed engine IDs. Template instantiations are
// described here once rather than generating a separate schema in each owner.
#include "Experiment/ModelData.h"
#include "../Physics/PhysicsTypes.h"
#include <reflgen/runtime/schema.h>
#include <reflgen/runtime/value_descriptor.h>

namespace reflgen
{
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
