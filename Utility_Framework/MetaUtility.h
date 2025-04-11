#pragma once
#include "TypeDefinition.h"

namespace Meta
{
    using Hash = uint32_t;

    template<typename ClassT, typename MemberT>
    constexpr std::ptrdiff_t GetMemberOffset(MemberT ClassT::* member)
    {
        return reinterpret_cast<std::ptrdiff_t>(
            &(reinterpret_cast<ClassT*>(0)->*member)
            );
    }

    template<typename T>
    inline std::string ToString()
    {
        std::string name = std::type_index(typeid(T)).name();

        const std::string struct_prefix = "struct ";
        const std::string class_prefix = "class ";
        const std::string enum_prefix = "enum ";

        if (name.find(struct_prefix) == 0)
            name = name.substr(struct_prefix.size());
        else if (name.find(class_prefix) == 0)
            name = name.substr(class_prefix.size());
        else if (name.find(enum_prefix) == 0)
            name = name.substr(enum_prefix.size());

        uint32_t pointerPos{};

        pointerPos = name.find(" *");
        if (pointerPos != std::string::npos)
        {
            name = name.substr(0, pointerPos);
        }

        return name;
    }

    // --- 문자열 해시 계산 (FNV-1a) ---
    inline constexpr Hash StringToHash(const char* str)
    {
        Hash hash_value = 2166136261u;
        while (*str)
        {
            hash_value ^= static_cast<Hash>(*str++);
            hash_value *= 16777619u;
        }

        return hash_value;
    }
}