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

        size_t pointerPos{};

        pointerPos = name.find(" *");
        if (pointerPos != std::string::npos)
        {
            name = name.substr(0, pointerPos);
        }

        return name;
    }

	inline std::string RemoveObjectPrefix(const std::string& name)
	{
		const std::string object_prefix = "class ";
		const std::string struct_prefix = "struct ";
		const std::string enum_prefix = "enum ";

		if (name.find(object_prefix) == 0)
			return name.substr(object_prefix.size());
		else if (name.find(struct_prefix) == 0)
			return name.substr(struct_prefix.size());
		else if (name.find(enum_prefix) == 0)
			return name.substr(enum_prefix.size());

		return name;
	}

    // Helper : vector 타입 여부 검사
    inline bool IsVectorType(const std::string& typeName)
    {
        return typeName.find("std::vector<") != std::string::npos;
    }

    // Helper : vector 내부 타입 추출
    inline std::string ExtractVectorElementType(const std::string& typeName)
    {
        auto start = typeName.find('<') + 1;
        auto end = typeName.find('>');
        return typeName.substr(start, end - start);
    }

	template<typename T>
    inline std::string GetVectorElementTypeName()
    {
        return RemoveObjectPrefix(ExtractVectorElementType(ToString<T>()));
    }

    inline static bool IsSharedPtr(std::string_view tn)
    {
        return tn.find("std::shared_ptr<") != std::string_view::npos;
    }

    inline static bool IsWeakPtr(std::string_view tn)
    {
        return tn.find("std::weak_ptr<") != std::string_view::npos;
    }

    inline static bool IsRawPtr(std::string_view tn)
    {
        return tn.size() && tn.back() == '*';
    }

    inline static std::string ExtractPointee(std::string_view tn) {
        // "std::shared_ptr<Foo>" -> "Foo"
        // "std::weak_ptr<Foo>"   -> "Foo"
        // "Foo*"                 -> "Foo"
        if (IsSharedPtr(tn)) {
            auto b = tn.find('<'); auto e = tn.rfind('>');
            return std::string(tn.substr(b + 1, e - b - 1));
        }
        if (IsWeakPtr(tn)) {
            auto b = tn.find('<'); auto e = tn.rfind('>');
            return std::string(tn.substr(b + 1, e - b - 1));
        }
        if (IsRawPtr(tn)) {
            return std::string(tn.substr(0, tn.size() - 1));
        }
        return std::string(tn);
    }
}