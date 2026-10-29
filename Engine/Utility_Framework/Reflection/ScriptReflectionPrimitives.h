#pragma once
// Runtime-only scalar adapters shared by production registration and native probes.
#include "ReflgenStringSerializers.h"
#include <reflgen/runtime/value_descriptor.h>
#include <source_location>
#include <filesystem>
#include <atomic>
#include <compare>
#include <limits>

namespace reflgen
{
    // Comparison results have a stable value representation without exposing
    // an implementation-defined comparison-category object layout.
    template<> struct runtime_value_traits<std::strong_ordering>
    {
        static constexpr value_shape shape = value_shape::signed_integer;
        static constexpr bool deserializable = false;
        static constexpr bool assignment_safe = false;
        using children = type_list<>;
        static void write(writer& out, const std::strong_ordering& value)
        {
            out.write_int(value < 0 ? -1 : value > 0 ? 1 : 0);
        }
    };

    // Keep the native file-clock epoch and tick period. This is an exact integer
    // timestamp, not a guessed conversion to Unix time or CLR DateTime ticks.
    template<> struct runtime_value_traits<std::filesystem::file_time_type>
    {
        static constexpr value_shape shape = value_shape::signed_integer;
        static constexpr bool assignment_safe = true;
        using children = type_list<>;
        using duration = std::filesystem::file_time_type::duration;
        using rep = duration::rep;
        static_assert(std::is_integral_v<rep> && std::is_signed_v<rep> && sizeof(rep) <= sizeof(std::int64_t));
        static void write(writer& out, const std::filesystem::file_time_type& value)
        {
            out.write_int(static_cast<std::int64_t>(value.time_since_epoch().count()));
        }
        static void read(reader& in, std::filesystem::file_time_type& value)
        {
            const std::int64_t ticks = in.read_int();
            if (ticks < (std::numeric_limits<rep>::min)() || ticks > (std::numeric_limits<rep>::max)())
            {
                throw serialization_error("native file-clock tick count is out of range");
            }
            value = std::filesystem::file_time_type(duration(static_cast<rep>(ticks)));
        }
    };

    // Completion/publication flags may be inspected without treating their
    // atomic object storage as bool or allowing a script to publish completion.
    template<> struct runtime_value_traits<std::atomic<bool>>
    {
        static constexpr value_shape shape = value_shape::boolean;
        static constexpr bool deserializable = false;
        static constexpr bool assignment_safe = false;
        using children = type_list<>;
        static void write(writer& out, const std::atomic<bool>& value)
        {
            out.write_bool(value.load(std::memory_order_acquire));
        }
    };

    // A filesystem path is a textual value, even though its C++ iterator exposes
    // component paths. Treating it as a recursive range would lose that value.
    template<> struct runtime_value_traits<std::filesystem::path>
    {
        static constexpr value_shape shape = value_shape::string;
        static constexpr bool assignment_safe = true;
        using children = type_list<>;
    };

    template<class Allocator>
    struct runtime_value_traits<ce::basic_utf8_string<Allocator>>
    {
        static constexpr value_shape shape = value_shape::string;
        static constexpr bool assignment_safe = true;
        using children = type_list<>;
    };

    template<class Allocator>
    struct runtime_value_traits<ce::basic_utf16_string<Allocator>>
    {
        static constexpr value_shape shape = value_shape::string;
        static constexpr bool assignment_safe = true;
        using children = type_list<>;
    };

    template<> struct runtime_value_traits<std::source_location>
    {
        static constexpr value_shape shape = value_shape::custom;
        static constexpr bool deserializable = false;
        static constexpr bool assignment_safe = false;
        using children = type_list<>;
        static void write(writer& out, const std::source_location& value)
        {
            out.begin_object(4);
            out.write_key("file"); out.write_string(value.file_name());
            out.write_key("function"); out.write_string(value.function_name());
            out.write_key("line"); out.write_uint(value.line());
            out.write_key("column"); out.write_uint(value.column());
            out.end_object();
        }
    };

    template<> struct runtime_value_traits<const char*>
    {
        static constexpr value_shape shape = value_shape::string;
        static constexpr bool deserializable = false;
        static constexpr bool assignment_safe = false;
        // Method argument storage owns a temporary UTF-8 string for the native
        // invocation. Persistent native pointer fields cannot be reassigned.
        static constexpr bool argument_safe = true;
        using children = type_list<>;
        static void write(writer& out, const char* const& value)
        {
            if (!value) { out.write_null(); return; }
            constexpr std::size_t maximum = 1024 * 1024;
            std::size_t length = 0;
            while (length < maximum && value[length] != '\0') ++length;
            if (length == maximum) throw serialization_error("borrowed UTF-8 string exceeds 1 MiB");
            out.write_string(std::string_view(value, length));
        }
    };

    template<> struct runtime_value_traits<ce::utf8_string_view>
    {
        static constexpr value_shape shape = value_shape::string;
        static constexpr bool deserializable = false;
        static constexpr bool assignment_safe = false;
        using children = type_list<>;
        static void write(writer& out, const ce::utf8_string_view& value)
        {
            out.write_string(std::string_view(value.data() ? value.data() : "", value.size()));
        }
    };

    template<> struct runtime_value_traits<ce::string_view>
    {
        static constexpr value_shape shape = value_shape::string;
        static constexpr bool deserializable = false;
        static constexpr bool assignment_safe = false;
        using children = type_list<>;
        static void write(writer& out, const ce::string_view& value)
        {
            ce::utf8_string bytes;
            Meta::StringSerialization::RequireUnicode(ce::to_utf8(value, bytes), "UTF-16");
            out.write_string(Meta::StringSerialization::StdView(bytes.view()));
        }
    };
}
