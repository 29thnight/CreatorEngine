#pragma once
// Runtime-only scalar adapters shared by production registration and native probes.
#include "ReflgenStringSerializers.h"
#include <reflgen/runtime/value_descriptor.h>
#include <source_location>
#include <filesystem>

namespace reflgen
{
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
