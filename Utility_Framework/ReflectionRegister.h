#pragma once
#include "ClassProperty.h"
#include "ReflectionType.h"
#include <functional>
#include <any>
#include <typeindex>

namespace Meta
{
    // --- TypeCaster: ��Ÿ�� Ÿ�� -> void* ��ȯ ---
    using AnyCaster = std::function<void* (const std::any&)>;

    class TypeCaster : public Singleton<TypeCaster>
    {
    public:
        friend Singleton;

    public:
        template<typename T>
        void RegisterSharedPtr()
        {
            _casters[typeid(std::shared_ptr<T>)] = [](const std::any& a) -> void*
            {
				//std::string typeName = ToString<T>();
                const auto& sp = std::any_cast<std::shared_ptr<T>>(a);
                return sp.get();  // ���� raw pointer ����
            };
        }

        template<typename T>
        void Register()
        {
            if constexpr (std::is_pointer_v<T>)
            {
                _casters[typeid(T)] = [](const std::any& a) -> void*
                {
                    return const_cast<void*>(static_cast<const void*>(std::any_cast<T>(a)));
                };
            }
            else
            {
                _casters[typeid(T)] = [](const std::any& a) -> void*
                {
                    const T& ref = std::any_cast<const T&>(a);  // reference cast
                    return const_cast<void*>(static_cast<const void*>(&ref));
                };
            }
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

			if (hashMap.find(type.typeID) == hashMap.end())
			{
				hashMap[type.typeID] = type;
			}
        }

        const Type* Find(const std::string& name)
        {
            auto it = map.find(name);
            return it != map.end() ? &it->second : nullptr;
        }

		const Type* Find(size_t typeID)
		{
			auto it = hashMap.find(typeID);
			return it != hashMap.end() ? &it->second : nullptr;
		}

    private:
        std::unordered_map<std::string, Type> map;
		std::unordered_map<size_t, Type> hashMap;
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
                if constexpr (requires { T::Create(); }) // Ŀ���� �޸�Ǯ ����
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