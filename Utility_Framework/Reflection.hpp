#pragma once
#include "TypeDefinition.h"
#include "ClassProperty.h"
#include "Core.Mathf.h"
#include <string>
#include <vector>
#include <array>
#include <span>
#include <any>
#include <functional>
#include <unordered_map>
#include <type_traits>
#include <stdexcept>
#include <imgui.h>

namespace Meta
{
    using Hash = uint32_t;
    // --- 컴파일 타임 타입 이름 추출 ---
    template<typename T>
    std::string ToString()
    {
        std::string name = std::type_index(typeid(T)).name();

        const std::string struct_prefix = "struct ";
        const std::string class_prefix = "class ";

        if (name.find(struct_prefix) == 0)
            name = name.substr(struct_prefix.size());
        else if (name.find(class_prefix) == 0)
            name = name.substr(class_prefix.size());

        uint32_t pointerPos{};

        pointerPos = name.find(" *");
        if(pointerPos != std::string::npos)
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

    struct Property
    {
        const char* name;
        std::string typeName;
        const std::type_info& typeInfo;
        std::function<std::any(void* instance)> getter;
        std::function<void(void* instance, std::any value)> setter;
    };

    template<typename ClassT, typename T>
    Property MakeProperty(const char* name, T ClassT::* member)
    {
        std::string typeStr = ToString<T>();

        if constexpr(std::is_pointer_v<T>)
        {
            TypeCast->Register<T>();
        }

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
            }
        };
    }

    struct MethodParameter {
        const char* name;
        const char* typeName;
        const std::type_info& typeInfo;
    };

    // --- Method: 멤버 함수 정보 ---
    struct Method
    {
        const char* name;
        std::function<std::any(void* instance, const std::vector<std::any>& args)> invoker;
        std::vector<MethodParameter> parameters;
    };

    template<typename ClassT, typename Ret, typename... Args, std::size_t... Is>
    Method MakeMethodImpl(const char* name, Ret(ClassT::* method)(Args...), std::index_sequence<Is...>) 
    {
        std::vector<MethodParameter> params = {
            MethodParameter{
                ("arg" + std::to_string(Is)).c_str(),
                ToString<Args>().data(),
                typeid(Args)
            }...
        };

        return {
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
    Method MakeMethod(const char* name, Ret(ClassT::* method)(Args...))
    {
        return MakeMethodImpl(name, method, std::index_sequence_for<Args...>{});
    }

    // --- Type: 하나의 타입 메타데이터 ---
    struct Type
    {
        const char* name{};
        std::span<const Property> properties{};
        std::span<const Method> methods{};
    };

    template<typename T, std::size_t N> using MetaContainer = std::array<T, N>;
    template<std::size_t N> using MetaProperties = MetaContainer<Meta::Property, N>;
	template<std::size_t N> using MetaMethods = MetaContainer<Meta::Method, N>;

    // --- Reflect() 존재 여부 concept ---
    template<typename T>
    concept HasReflect = requires
    {
        { T::Reflect() } -> std::same_as<const Type&>;
    };

    // --- Registry: 전역 타입 등록소 ---
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

    template<typename T> requires HasReflect<T>
    static inline void Register()
    {
        const Type& type = T::Reflect();
        MetaDataRegistry->Register(type.name, type);
    }

	static inline const Type* Find(const std::string_view& name)
	{
		return MetaDataRegistry->Find(name.data());
	}

    // --- Invoke 메서드 by 이름 ---
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

    inline void DrawObject(void* instance, const Type& type);

    // --- ImGui 렌더링 ---
    inline void DrawProperties(void* instance, const Type& type) 
    {
        for (const auto& prop : type.properties) 
        {
            const Hash hash = StringToHash(prop.typeName.c_str());
            if (hash == StringToHash("int")) 
            {
                int value = std::any_cast<int>(prop.getter(instance));
                if (ImGui::InputInt(prop.name, &value)) 
                {
                    prop.setter(instance, value);
                }
            }
            else if (hash == StringToHash("float")) 
            {
                float value = std::any_cast<float>(prop.getter(instance));
                if (ImGui::InputFloat(prop.name, &value)) 
                {
                    prop.setter(instance, value);
                }
            }
            else if (hash == StringToHash("bool") || hash == StringToHash("bool32"))
            {
                bool value = std::any_cast<bool>(prop.getter(instance));
                if (ImGui::Checkbox(prop.name, &value)) 
                {
                    prop.setter(instance, value);
                }
            }
			else if (hash == StringToHash("std::string"))
			{
				std::string value = std::any_cast<std::string>(prop.getter(instance));
				if (ImGui::InputText(prop.name, value.data(), value.size() + 1))
				{
					prop.setter(instance, value);
				}
			}
            else if (hash == StringToHash("Mathf::Vector2") || hash == StringToHash("DirectX::SimpleMath::Vector2"))
            {
				auto value = std::any_cast<Mathf::Vector2>(prop.getter(instance));
				if (ImGui::InputFloat2(prop.name, &value.x))
				{
					prop.setter(instance, value);
				}
			}
			else if (hash == StringToHash("Mathf::Vector3") || hash == StringToHash("DirectX::SimpleMath::Vector3"))
			{
				auto value = std::any_cast<Mathf::Vector3>(prop.getter(instance));
				if (ImGui::InputFloat3(prop.name, &value.x))
				{
					prop.setter(instance, value);
				}
			}
			else if (hash == StringToHash("Mathf::Vector4") || hash == StringToHash("DirectX::SimpleMath::Vector4"))
			{
				auto value = std::any_cast<Mathf::Vector4>(prop.getter(instance));
				if (ImGui::InputFloat4(prop.name, &value.x))
				{
					prop.setter(instance, value);
				}
			}
			else if (hash == StringToHash("Mathf::Color4") || hash == StringToHash("DirectX::SimpleMath::Color"))
			{
				auto value = std::any_cast<Mathf::Color4>(prop.getter(instance));
				if (ImGui::ColorEdit4(prop.name, &value.x))
				{
					prop.setter(instance, value);
				}
            }
            else if (hash == StringToHash("float2") || hash == StringToHash("DirectX::XMFLOAT2"))
            {
				auto value = std::any_cast<float2>(prop.getter(instance));
				if (ImGui::InputFloat2(prop.name, &value.x))
				{
					prop.setter(instance, value);
				}
			}
			else if (hash == StringToHash("float3") || hash == StringToHash("DirectX::XMFLOAT3"))
			{
				auto value = std::any_cast<float3>(prop.getter(instance));
				if (ImGui::InputFloat3(prop.name, &value.x))
				{
					prop.setter(instance, value);
				}
			}
			else if (hash == StringToHash("float4") || hash == StringToHash("DirectX::XMFLOAT4"))
			{
				auto value = std::any_cast<float4>(prop.getter(instance));
				if (ImGui::InputFloat4(prop.name, &value.x))
				{
					prop.setter(instance, value);
				}
			}
			else if (hash == StringToHash("int2") || hash == StringToHash("DirectX::XMINT2"))
			{
				auto value = std::any_cast<int2>(prop.getter(instance));
				if (ImGui::InputInt2(prop.name, &value.x))
				{
					prop.setter(instance, value);
				}
			}
			else if (hash == StringToHash("int3") || hash == StringToHash("DirectX::XMINT3"))
			{
				auto value = std::any_cast<int3>(prop.getter(instance));
				if (ImGui::InputInt3(prop.name, &value.x))
				{
					prop.setter(instance, value);
				}
			} // 다른 타입 추가 가능
            else 
            {
                void* ptr = TypeCast->ToVoidPtr(prop.typeInfo, prop.getter(instance));
                if (ptr) 
                {
                    std::string_view view = prop.typeName.c_str();
                    size_t endPos = view.rfind("*");
                    std::string name = view.data();
                    std::string copyname = name.substr(0, endPos);
                    if (const Type* subType = MetaDataRegistry->Find(copyname))
                    {
                        ImGui::PushID(prop.name);
                        ImGui::Text("%s:", prop.name);
                        DrawObject(ptr, *subType);
                        ImGui::PopID();
                    }
                    else 
                    {
                        ImGui::Text("%s: [Unregistered Type]", prop.name);
                    }
                }
                else 
                {
                    ImGui::Text("%s: nullptr", prop.name);
                }
            }
        }
    }

    inline void DrawMethods(void* instance, const Type& type) 
    {
        for (const auto& method : type.methods) 
        {
            if (ImGui::Button(method.name)) 
            {
                try 
                {
                    method.invoker(instance, {}); // 인자 없는 함수만 지원
                }
                catch (const std::exception& e) 
                {
                    ImGui::TextColored(ImVec4(1, 0, 0, 1), "Error: %s", e.what());
                }
            }
        }
    }

    inline void DrawObject(void* instance, const Type& type) 
    {
        ImGui::Indent();
        DrawProperties(instance, type);
        DrawMethods(instance, type);
        ImGui::Unindent();
    }

} // namespace Meta

#define meta_default(T) { Meta::Register<T>(); }