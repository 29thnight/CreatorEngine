#pragma once

#include "ReflectionString.h"
#include <reflgen/serial/serializer.h>

// reflgen owns the std::string/string_view virtual ABI. Engine storage crosses
// that boundary with an explicit length; UTF-16 is strictly converted to UTF-8.
// Keep this independent of scene/reflection registries so the actual serializers
// can be exercised with every writer/reader backend.
namespace Meta::StringSerialization
{
    inline std::string_view StdView(ce::utf8_string_view value) noexcept
    {
        return {value.data() ? value.data() : "", value.size()};
    }

    inline ce::utf8_string_view Utf8View(std::string_view value) noexcept
    {
        return {value.data(), value.size()};
    }

    inline void RequireUnicode(ce::unicode_result result, const char* encoding)
    {
        if (!result)
        {
            throw reflgen::serialization_error(std::string("invalid ") + encoding
                + " string at code unit " + std::to_string(result.offset));
        }
    }

    template<meta::text::OwningString T>
    inline std::string EncodeKey(const T& value)
    {
        if constexpr (meta::text::Utf8String<T>)
        {
            return std::string(value.data(), value.size());
        }
        else
        {
            ce::utf8_string bytes;
            RequireUnicode(ce::to_utf8(value.view(), bytes), "UTF-16");
            return std::string(bytes.data(), bytes.size());
        }
    }

    template<meta::text::OwningString T>
    inline void Decode(std::string_view text, T& value)
    {
        if constexpr (meta::text::Utf8String<T>)
        {
            value.assign(Utf8View(text));
        }
        else
        {
            // Validation precedes mutation, including malformed UTF-8 from a
            // permissive backend. Keep both the previous value and allocator.
            RequireUnicode(ce::to_utf16(Utf8View(text), value), "UTF-8");
        }
    }

    // The pinned reflgen key codec only recognizes std::basic_string<char>.
    // Explicitly keep unique ce-string-keyed maps as objects, matching the
    // pre-migration string-key map format. Multimaps retain reflgen's pair array.
    template<class C>
    concept StringKeyedMap = requires
    {
        typename reflgen::detail::container_traits_of<C>::key_type;
        requires meta::text::OwningString<typename reflgen::detail::container_traits_of<C>::key_type>;
        requires reflgen::detail::container_traits_of<C>::kind == reflgen::container_kind::map;
        requires reflgen::detail::container_traits_of<C>::unique_keys;
    };
}

namespace reflgen
{
    template<class Allocator>
    struct serializer<ce::basic_utf8_string<Allocator>>
    {
        static void write(writer& out, const ce::basic_utf8_string<Allocator>& value)
        {
            out.write_string(Meta::StringSerialization::StdView(value.view()));
        }

        static void read(reader& in, ce::basic_utf8_string<Allocator>& value)
        {
            const std::string text = in.read_string();
            Meta::StringSerialization::Decode(text, value);
        }
    };

    template<class Allocator>
    struct serializer<ce::basic_utf16_string<Allocator>>
    {
        static void write(writer& out, const ce::basic_utf16_string<Allocator>& value)
        {
            ce::utf8_string bytes;
            Meta::StringSerialization::RequireUnicode(ce::to_utf8(value.view(), bytes), "UTF-16");
            out.write_string(Meta::StringSerialization::StdView(bytes.view()));
        }

        static void read(reader& in, ce::basic_utf16_string<Allocator>& value)
        {
            const std::string text = in.read_string();
            Meta::StringSerialization::Decode(text, value);
        }
    };

    template<Meta::StringSerialization::StringKeyedMap C>
    struct serializer<C>
    {
        using traits = detail::container_traits_of<C>;

        static void write(writer& out, const C& value)
        {
            out.begin_object(traits::size(value));
            traits::for_each(value, [&](const auto& key, const auto& mapped)
            {
                const std::string text = Meta::StringSerialization::EncodeKey(key);
                out.write_key(text);
                detail::with_path(text, [&] { serialize(out, mapped); });
            });
            out.end_object();
        }

        static void read(reader& in, C& value)
        {
            in.begin_object();
            traits::clear(value);
            std::string text;
            while (in.next_key(text))
            {
                detail::with_path(text, [&]
                {
                    typename traits::key_type key;
                    Meta::StringSerialization::Decode(text, key);
                    typename traits::mapped_type mapped{};
                    deserialize(in, mapped);
                    traits::add(value, std::move(key), std::move(mapped));
                });
            }
            in.end_object();
        }
    };
}
