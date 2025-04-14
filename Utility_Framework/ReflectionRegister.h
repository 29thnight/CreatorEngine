#pragma once
#include "ClassProperty.h"
#include "ReflectionType.h"
#include <functional>
#include <any>
#include <typeindex>

namespace Meta
{
    // --- TypeCaster: 런타임 타입 -> void* 변환 ---
    using AnyCaster = std::function<void* (const std::any&)>;

    class TypeCaster : public Singleton<TypeCaster>
    {
    public:
        friend Singleton;

    public:
        template<typename T>
        void Register()
        {
            _casters[typeid(T)] = [](const std::any& a) -> void*
                {
                    return const_cast<void*>(static_cast<const void*>(std::any_cast<T>(a)));
                };
        }

        void* ToVoidPtr(const std::type_info& ti, const std::any& a)
        {
            auto it = _casters.find(ti);
            return (it != _casters.end()) ? it->second(a) : nullptr;
        }

    private:
        std::unordered_map<std::type_index, AnyCaster> _casters;
    };

    static inline auto& TypeCast = TypeCaster::GetInstance();

    class Registry : public Singleton<Registry>
    {
    public:
        friend Singleton;

        void Register(const std::string& name, const Type& type)
        {
            if (map.find(name) == map.end())
            {
                map[name] = type;
            }
        }

        const Type* Find(const std::string& name)
        {
            auto it = map.find(name);
            return it != map.end() ? &it->second : nullptr;
        }

    private:
        std::unordered_map<std::string, Type> map;
    };

    static inline auto& MetaDataRegistry = Registry::GetInstance();

    class EnumRegistry : public Singleton<EnumRegistry>
    {
    public:
		friend Singleton;
        void Register(const std::string& name, const EnumType& enumType)
        {
            if (enumMap.find(name) == enumMap.end())
            {
                enumMap[name] = enumType;
            }
        }

        const EnumType* Find(const std::string& name)
        {
            auto it = enumMap.find(name);
            return (it != enumMap.end()) ? &it->second : nullptr;
        }

    private:
        std::unordered_map<std::string, EnumType> enumMap;
    };

    static inline auto& MetaEnumRegistry = EnumRegistry::GetInstance();

    using FactoryFunction = std::function<void*()>;

    class FactoryRegistry : public Singleton<FactoryRegistry>
    {
    public:
        friend Singleton;

        template<typename T>
        void Register()
        {
            _factories[ToString<T>()] = []() -> void*
                {
                    if constexpr (requires { T::Create(); }) // 커스텀 메모리풀 지원
                    {
                        return T::Create();
                    }
                    else
                    {
                        return new T();
                    }
                };
        }

        void* Create(const std::string& typeName)
        {
            auto it = _factories.find(typeName);
            return (it != _factories.end()) ? it->second() : nullptr;
        }

    private:
        std::unordered_map<std::string, FactoryFunction> _factories;
    };

    static inline auto& MetaFactoryRegistry = FactoryRegistry::GetInstance();

    template <typename Enum>
    struct EnumAutoRegistrar
    {
        EnumAutoRegistrar()
        {
            auto enumType = create_enum_type<Enum>();
            MetaEnumRegistry->Register(enumType.name, enumType);
        }
    };
}