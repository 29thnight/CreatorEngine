#pragma once
#include <span>

namespace Meta
{
    struct Property
    {
        const char* name;
        std::string typeName;
        const std::type_info& typeInfo;
        std::function<std::any(void* instance)> getter;
        std::function<void(void* instance, std::any value)> setter;
        bool isPointer;
        std::ptrdiff_t offset;  // �߰�: ��� ������
    };

    struct MethodParameter
    {
        std::string name;
        std::string typeName;
        const std::type_info& typeInfo;
    };

    // --- Method: ��� �Լ� ���� ---
    struct Method
    {
        const char* name;
        std::function<std::any(void* instance, const std::vector<std::any>& args)> invoker;
        std::vector<MethodParameter> parameters;
    };

    // --- Type: �ϳ��� Ÿ�� ��Ÿ������ ---
    struct Type
    {
        const char* name{};
        std::span<const Property> properties{};
        std::span<const Method> methods{};
        const Type* parent = nullptr;  // �θ� Ÿ���� ����Ű�� ������ (������ nullptr)
    };

    struct EnumValue 
    {
        const char* name;
        int value;
    };

    struct EnumType 
    {
        const char* name;
        std::span<const EnumValue> values;
    };

    template<typename T, std::size_t N> using MetaContainer = std::array<T, N>;
    template<std::size_t N> using MetaProperties = MetaContainer<Meta::Property, N>;
    template<std::size_t N> using MetaMethods = MetaContainer<Meta::Method, N>;

    // --- Reflect() ���� ���� concept ---
    template<typename T>
    concept HasReflect = requires
    {
        { T::Reflect() } -> std::same_as<const Type&>;
    };
}
