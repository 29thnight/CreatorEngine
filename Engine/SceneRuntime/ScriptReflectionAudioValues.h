#pragma once
// Runtime wire adapters preserve audio identity independently of authoring files.
#include "Audio/AudioValues.h"
#include <reflgen/runtime/value_descriptor.h>

namespace reflgen
{
    template<> struct runtime_value_traits<wave::ClipKey>
    {
        static constexpr bool assignment_safe = true;
        using children = type_list<std::string, bool>;

        static void write(writer& out, const wave::ClipKey& value)
        {
            out.begin_object(2);
            out.write_key("text");
            out.write_string(value.Text());
            out.write_key("isGuid");
            out.write_bool(value.IsGuid());
            out.end_object();
        }

        static void read(reader& in, wave::ClipKey& value)
        {
            std::string text;
            std::string key;
            bool isGuid = false;
            bool hasText = false;
            bool hasKind = false;
            in.begin_object();
            while (in.next_key(key))
            {
                if (key == "text" && !hasText)
                {
                    text = in.read_string();
                    hasText = true;
                }
                else if (key == "isGuid" && !hasKind)
                {
                    isGuid = in.read_bool();
                    hasKind = true;
                }
                else
                {
                    throw serialization_error("native clip key has an unknown or duplicate member");
                }
            }
            in.end_object();
            if (!hasText || !hasKind)
            {
                throw serialization_error("native clip key requires text and isGuid");
            }
            if (isGuid)
            {
                const uuid::uuid16 guid = uuid::parse(text);
                if (guid.is_nil())
                {
                    throw serialization_error("native clip GUID must be nonzero");
                }
                value = wave::ClipKey::FromGuid(guid);
            }
            else
            {
                value = wave::ClipKey(std::move(text));
            }
        }
    };
}
