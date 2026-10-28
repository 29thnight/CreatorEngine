#pragma once

#include "../../Containers/ContainerAPI.h"
#include "../Foundation/StandardLibrary.h"

namespace meta::text
{
    template<class T> inline constexpr bool is_utf8_string = false;
    template<class Allocator>
    inline constexpr bool is_utf8_string<ce::basic_utf8_string<Allocator>> = true;

    template<class T> inline constexpr bool is_utf16_string = false;
    template<class Allocator>
    inline constexpr bool is_utf16_string<ce::basic_utf16_string<Allocator>> = true;

    template<class T>
    concept Utf8String = is_utf8_string<std::remove_cvref_t<T>>;
    template<class T>
    concept Utf16String = is_utf16_string<std::remove_cvref_t<T>>;
    template<class T>
    concept OwningString = Utf8String<T> || Utf16String<T>;
    template<class T>
    concept StringLike = OwningString<T>
        || std::is_same_v<std::remove_cvref_t<T>, ce::utf8_string_view>
        || std::is_same_v<std::remove_cvref_t<T>, ce::string_view>;
}
