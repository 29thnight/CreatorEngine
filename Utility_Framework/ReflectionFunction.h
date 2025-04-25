#pragma once
#include "TypeTrait.h"
#include "MetaUtility.h"
#include "ReflectionType.h"
#include "ReflectionRegister.h"
#include "Core.Mathf.h"
#include "LogSystem.h"
#include "HashingString.h"
#include <imgui.h>
#include <magic_enum/magic_enum.hpp>

namespace Meta
{
    template<typename T> requires HasReflect<T>
    static inline void Register()
    {
        const Type& type = T::Reflect();
        MetaDataRegistry->Register(type.name, type);
        MetaFactoryRegistry->Register<T>();
    }

    static inline const Type* Find(const std::string_view& name)
    {
        return MetaDataRegistry->Find(name.data());
    }

	static inline const EnumType* FindEnum(const std::string_view& name)
	{
		return MetaEnumRegistry->Find(name.data());
	}

	static inline const Type* Find(size_t typeID)
	{
		return MetaDataRegistry->Find(typeID);
	}

    template <typename Enum, std::size_t... Is>
    auto create_enum_values(std::index_sequence<Is...>)
    {
        return std::array<EnumValue, magic_enum::enum_count<Enum>()>
        {
            EnumValue{ magic_enum::enum_names<Enum>()[Is].data(), static_cast<int>(magic_enum::enum_values<Enum>()[Is]) }...
        };
    }

    template <typename Enum>
    const EnumType& create_enum_type()
    {
        static const auto values = create_enum_values<Enum>(std::make_index_sequence<magic_enum::enum_count<Enum>()>{});
        static const EnumType enumType
        {
            magic_enum::enum_type_name<Enum>().data(),
            std::span<const EnumValue>(values.data(), values.size())
        };
        return enumType;
    }

    template<typename ClassT, typename T>
    Property MakePropertyImpl(const char* name, T ClassT::* member)
    {
        std::string typeStr = ToString<T>();
		HashedGuid typeID = TypeTrait::GUIDCreator::GetTypeID<T>();
        bool isPointer = std::is_pointer_v<T> || is_shared_ptr_v<T>;
		bool isVector = is_vector_v<T>;
		bool isElementPointer = false;

        if constexpr (std::is_pointer_v<T>)
        {
            using Pointee = std::remove_pointer_t<T>;
            TypeCast->Register<T>();
        }
        else if constexpr (is_shared_ptr_v<T>)
        {
            using Pointee = typename T::element_type;
            TypeCast->Register<T>();
        }
        else if constexpr (requires { typename VectorElementType<T>::Type; })
        {
            using ElemType = VectorElementTypeT<T>;

            if constexpr (std::is_pointer_v<ElemType>)
            {
                using Pointee = std::remove_pointer_t<ElemType>;
				isElementPointer = true;
                TypeCast->Register<Pointee>();
            }
            else if constexpr (is_shared_ptr_v<ElemType>)
            {
                using Pointee = typename ElemType::element_type;
				isElementPointer = true;
                TypeCast->Register<Pointee>();
				TypeCast->RegisterSharedPtr<Pointee>();
            }
        }

        if constexpr (Meta::HasReflect<std::remove_cvref_t<T>>)
        {
            Meta::Register<std::remove_cvref_t<T>>();
        }

        std::ptrdiff_t offset = GetMemberOffset(member);

        if constexpr (is_vector_v<T>)
        {
            using ElementType = VectorElementTypeT<T>;

			if constexpr (std::is_pointer_v<ElementType>)
			{
				using Pointee = std::remove_pointer_t<ElementType>;
                return
                {
                    name,
                    typeStr.c_str(),
                    typeid(T),
                    [member](void* instance) -> std::any
                    {
                        return static_cast<ClassT*>(instance)->*member;
                    },
                    [member](void* instance, std::any value)
                    {
                        static_cast<ClassT*>(instance)->*member = std::any_cast<T>(value);
                    },
                    isPointer,
                    offset,
                    typeID,
                    isVector,
                    typeid(ElementType),
                    [member](void* instance) -> std::unique_ptr<IVectorIterator>
                    {
                        auto vecPtr = &(static_cast<ClassT*>(instance)->*member);
                        return std::make_unique<VectorIteratorImpl<ElementType>>(vecPtr->begin(), vecPtr->end());
                    },
                    GetVectorElementTypeName<ElementType>(),
                    TypeTrait::GUIDCreator::GetTypeID<Pointee>(),
                    isElementPointer,
                };
			}
            else if constexpr (is_shared_ptr_v<ElementType>)
            {
                using Pointee = typename ElementType::element_type;

				size_t typeID = TypeTrait::GUIDCreator::GetTypeID<Pointee>();

                return
                {
                    name,
                    typeStr.c_str(),
                    typeid(T),
                    [member](void* instance) -> std::any
                    {
                        return static_cast<ClassT*>(instance)->*member;
                    },
                    [member](void* instance, std::any value)
                    {
                        static_cast<ClassT*>(instance)->*member = std::any_cast<T>(value);
                    },
                    isPointer,
                    offset,
                    typeID,
                    isVector,
                    typeid(ElementType),
                    [member](void* instance) -> std::unique_ptr<IVectorIterator>
                    {
                        auto vecPtr = &(static_cast<ClassT*>(instance)->*member);
                        return std::make_unique<VectorIteratorImpl<ElementType>>(vecPtr->begin(), vecPtr->end());
                    },
                    GetVectorElementTypeName<ElementType>(),
                    TypeTrait::GUIDCreator::GetTypeID<Pointee>(),
                    isElementPointer,
                };
            }
            else
            {
                return
                {
                    name,
                    typeStr.c_str(),
                    typeid(T),
                    [member](void* instance) -> std::any
                    {
                        return &(static_cast<ClassT*>(instance)->*member);
                    },
                    [member](void* instance, std::any value)
                    {
                        (static_cast<ClassT*>(instance)->*member) = *std::any_cast<T*>(value);
                    },
                    isPointer,
                    offset,
                    typeID,
                    isVector,
                    typeid(ElementType),
                    [member](void* instance) -> std::unique_ptr<IVectorIterator>
                    {
                        auto vecPtr = &(static_cast<ClassT*>(instance)->*member);
                        return std::make_unique<VectorIteratorImpl<ElementType>>(vecPtr->begin(), vecPtr->end());
                    },
                    GetVectorElementTypeName<ElementType>(),
                    isElementPointer
                };
            }
        }
        else
        {
            return
            {
                name,
                typeStr.c_str(),
                typeid(T),
                [member](void* instance) -> std::any
                {
                    return static_cast<ClassT*>(instance)->*member;
                },
                [member](void* instance, std::any value)
                {
                    static_cast<ClassT*>(instance)->*member = std::any_cast<T>(value);
                },
                isPointer,
                offset,
                typeID,
                isVector,
                typeid(T),
            };
        }
    }

    template<typename ClassT, typename EnumT>
    std::enable_if_t<std::is_enum_v<EnumT>, Property>
        MakeEnumPropertyImpl(const char* name, EnumT ClassT::* member)
    {
        return Property
        {
            name,
            ToString<EnumT>(),
            typeid(EnumT),
            [member](void* instance) -> std::any {
                EnumT value = static_cast<ClassT*>(instance)->*member;
                using Underlying = std::underlying_type_t<EnumT>;
                return static_cast<int>(static_cast<Underlying>(value));
            },
            [member](void* instance, std::any anyValue) {
                int intValue = std::any_cast<int>(anyValue);
                static_cast<ClassT*>(instance)->*member = static_cast<EnumT>(intValue);
            },
            false,
            GetMemberOffset(member),
			TypeTrait::GUIDCreator::GetTypeID<EnumT>(),
			false,
			typeid(EnumT),
        };
    }

    template<typename ClassT, typename T>
    Property MakeProperty(const char* name, T ClassT::* member)
    {
        if constexpr (std::is_enum_v<T>)
        {
            return MakeEnumPropertyImpl(name, member);
        }
        else
        {
            return MakePropertyImpl(name, member);
        }
    }

    template<typename ClassT, typename Ret, typename... Args, std::size_t... Is>
    Method MakeMethodImpl(
        const char* name,
        Ret(ClassT::* method)(Args...),
        std::index_sequence<Is...>,
        const std::vector<std::string>& paramNames)
    {
        std::vector<MethodParameter> params = {
            MethodParameter{
                (Is < paramNames.size() ? paramNames[Is] : ("arg" + std::to_string(Is))),
                ToString<Args>(),
                typeid(Args),
				TypeTrait::GUIDCreator::GetTypeID<Args>()
            }...
        };

        return
        {
            name,
            [method](void* instance, const std::vector<std::any>& args) -> std::any
            {
                if (args.size() != sizeof...(Args))
                    throw std::runtime_error("Argument count mismatch");

                auto call = [=]<std::size_t... I>(std::index_sequence<I...>) -> std::any
                {
                    if constexpr (std::is_void_v<Ret>)
                    {
                        (static_cast<ClassT*>(instance)->*method)(
                            std::any_cast<std::remove_reference_t<Args>>(args[I])...
                        );
                        return {};
                    }
                    else
                    {
                        return (static_cast<ClassT*>(instance)->*method)(
                              std::any_cast<std::remove_reference_t<Args>>(args[I])...
                        );
                    }
                };

                return call(std::index_sequence_for<Args...>{});
            },
            std::move(params)
        };
    }

    template<typename ClassT, typename Ret, typename... Args>
    Method MakeMethod(const char* name, Ret(ClassT::* method)(Args...), const std::vector<std::string>& paramNames = {})
    {
        return MakeMethodImpl(name, method, std::index_sequence_for<Args...>{}, paramNames);
    }

    inline bool InvokeMethodByMetaName(void* instance, const Type& type, const std::string& methodName, const std::vector<std::any>& args, std::any* outResult = nullptr)
    {
        for (const auto& method : type.methods)
        {
            if (method.name == methodName)
            {
                std::any result = method.invoker(instance, args);
                if (outResult) *outResult = std::move(result);
                return true;
            }
        }
        return false;
    }

	template <typename T>
    inline void MakePropChangeCommand(void* instance, const Property& prop, const T& value)
    {
		T prevValue = std::any_cast<T>(prop.getter(instance));
		UndoCommandManager->Execute(
			std::make_unique<PropertyChangeCommand<T>>(instance, prop, prevValue, value)
		);
    }

	inline void MakeCustomChangeCommand(std::function<void()> undoFunc, std::function<void()> redoFunc)
	{
		UndoCommandManager->Execute(
			std::make_unique<CustomChangeCommand>(undoFunc, redoFunc)
		);
	}
}
