#pragma once
// CT3: ClassProperty.h·yaml-cpp(본문 사용 0회 — 죽은 include)와
// MetaStateCommand.h·<stack>(UndoManager 전속 → ReflectionUndo.h로 이관)을
// 잘랐다. 이 헤더는 등록 코어(TypeCaster·Registry·EnumRegistry·Factory)만 남는다.
#include "ReflectionType.h"
#include "ManagedHeapObject.h"
#include "DLLAcrossSingleton.h"
#include <functional>
#include <any>
#include <typeindex>

class ComponentFactory;
namespace Meta
{
    // --- TypeCaster: 런타임 타입 -> void* 변환 ---
    using AnyCaster = std::function<void* (const std::any&)>;

    class TypeCaster : public DLLCore::Singleton<TypeCaster>
    {
    private:
        TypeCaster() = default;
        ~TypeCaster() = default;
        friend DLLCore::Singleton<TypeCaster>;
    public:

    public:
        // 스코프 추적 기계(BeginScope/EndScope/UnRegisterScope + 참조 카운트 맵
        // 5종)는 CT2에서 삭제했다 — C++ 스크립트 핫리로드(ModuleBehavior)가
        // 9-4에서 은퇴한 뒤 호출처 0건이었다. 등록은 이제 축적만 한다.
        template<typename T>
        void RegisterSharedPtr()
        {
            const std::type_index casterKey{ typeid(std::shared_ptr<T>) };
            _casters[casterKey] = [](const std::any& a) -> void*
            {
                const auto& sp = std::any_cast<std::shared_ptr<T>>(a);
                return const_cast<void*>(static_cast<const void*>(sp.get()));  //  raw pointer
            };
        }

        template<typename T>
        void Register()
        {
            const std::type_index casterKey{ typeid(T) };
            if constexpr (std::is_pointer_v<T>)
            {
                _casters[casterKey] = [](const std::any& a) -> void*
                {
                    return const_cast<void*>(static_cast<const void*>(std::any_cast<T>(a)));
                };
            }
            else
            {
                _casters[casterKey] = [](const std::any& a) -> void*
                {
                    const T& ref = std::any_cast<const T&>(a);  // reference cast
                    return const_cast<void*>(static_cast<const void*>(&ref));
                };
            }
        }

        template<typename T>
        void RegisterMakeAny()
        {
            const std::type_index ptrKey{ typeid(T*) };
            _makeAny[ptrKey] = [](void* ptr) -> std::any {
                return static_cast<T*>(ptr);
                };

            const std::type_index sharedKey{ typeid(std::shared_ptr<T>) };
            _makeAny[sharedKey] = [](void* ptr) -> std::any {
                return std::shared_ptr<T>(static_cast<T*>(ptr));
                };
        }

        void* ToVoidPtr(const std::type_info& ti, const std::any& a)
        {
            auto it = _casters.find(ti);
            return (it != _casters.end()) ? it->second(a) : nullptr;
        }

        std::any MakeAnyFromRaw(const std::type_info& ti, void* ptr)
        {
            auto it = _makeAny.find(ti);
            if (it != _makeAny.end())
                return it->second(ptr);

            return {}; // 변환 실패
        }

    private:
        std::unordered_map<std::type_index, AnyCaster> _casters;
        std::unordered_map<std::type_index, std::function<std::any(void*)>> _makeAny;
    };

    inline auto TypeCast = TypeCaster::GetInstance();

    class Registry : public DLLCore::Singleton<Registry>
    {
    private:
        Registry() = default;
        ~Registry() = default;
        friend DLLCore::Singleton<Registry>;
        friend class ::ComponentFactory;
    public:
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

        // ScriptRegister/UnRegister(스크립트 핫리로드용 갱신·해제)는 CT2에서
        // 삭제 — 호출처 0건. 덕분에 이름 맵·해시 맵의 탈동기화 창구(분석 F-4)도
        // 등록 단일 경로로 좁혀졌다.

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

        // 등록 타입 전수 열람(PHASE 18 CT0 — reflect.golden). 복사본을 돌려주는
        // 이유: 호출자는 정렬해 쓰는데, 맵 내부 참조를 내주면 순회 중 등록/해제와
        // 겹칠 때의 안전을 호출자가 증명해야 한다.
        std::vector<std::string> GetAllTypeNames() const
        {
            std::vector<std::string> names;
            names.reserve(map.size());
            for (const auto& [name, type] : map)
            {
                names.push_back(name);
            }
            return names;
        }

    private:
        std::unordered_map<std::string, Type> map;
        std::unordered_map<size_t, Type> hashMap;
    };

    inline auto MetaDataRegistry = Registry::GetInstance();

    class EnumRegistry : public DLLCore::Singleton<EnumRegistry>
    {
    private:
        EnumRegistry() = default;
        ~EnumRegistry() = default;
        friend DLLCore::Singleton<EnumRegistry>;
    public:
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

    inline auto MetaEnumRegistry = EnumRegistry::GetInstance();
    using FactoryFunction = std::function<void* ()>;
    using SharedFactoryFunction = std::function<std::shared_ptr<void>()>;
    class IRegistableEvent;
    class FactoryRegistry : public DLLCore::Singleton<FactoryRegistry>
    {
    private:
        friend DLLCore::Singleton<FactoryRegistry>;
        FactoryRegistry() = default;
        ~FactoryRegistry() = default;

    public:
        template<typename T>
        void Register()
        {
            if constexpr (std::is_base_of_v<Managed::HeapObject, T>)
            {
                _sharedFactories[ToString<T>()] = []() -> std::shared_ptr<T>
                    {
                        return shared_alloc<T>();
                    };

                _factories[ToString<T>()] = []() -> T*
                    {
                        return new T();
                    };
            }
            else
            {
                _factories[ToString<T>()] = []() -> T*
                    {
                        return new T();
                    };
            }
        }

        void* Create(const std::string& typeName)
        {
            auto it = _factories.find(typeName);
            return (it != _factories.end()) ? it->second() : nullptr;
        }

        std::shared_ptr<void> CreateShared(const std::string& typeName)
        {
            auto it = _sharedFactories.find(typeName);
            if (it != _sharedFactories.end())
            {
                return it->second();
            }

            return nullptr; // 해당 타입의 팩토리가 없으면 nullptr 반환
        }

        template<typename T>
        T* Create(const std::string& typeName)
        {
            auto it = _factories.find(typeName);
            if (it != _factories.end())
            {
                return static_cast<T*>(it->second());
            }
            return nullptr;
        }

        template<typename T>
        std::shared_ptr<T> CreateShared(const std::string& typeName)
        {
            auto it = _sharedFactories.find(typeName);
            if (it != _sharedFactories.end())
            {
                return std::static_pointer_cast<T>(it->second());
            }
            return nullptr; // 해당 타입의 팩토리가 없으면 nullptr 반환
        }

    private:
        std::unordered_map<std::string, FactoryFunction> _factories;
        std::unordered_map<std::string, SharedFactoryFunction> _sharedFactories;
    };

    inline auto MetaFactoryRegistry = FactoryRegistry::GetInstance();

    // UndoManager·UndoCommandManager는 CT3에서 ReflectionUndo.h로 이관 —
    // 등록 코어와 무관한 에디터 계층이 MetaStateCommand·<stack>을 여기 소비자
    // 전원에게 실어 나르고 있었다.

    template <typename Enum>
    struct EnumAutoRegistrar
    {
        EnumAutoRegistrar()
        {
            auto enumType = create_enum_type<Enum>();
            EnumRegistry::GetInstance()->Register(enumType.name, enumType);
        }
    };

    // ClassAutoRegistrar는 CT2에서 삭제 — 인스턴스화 0건. 클래스 등록의 정본은
    // 등록 정본(RegisterReflectManual.h)의 Meta::Register<T>() 직접 호출이다.

    // Undo 계층의 init/final은 ReflectionUndo.h의 UndoSystemInitialize/Finalize로
    // 이관 — 호출처는 EngineBootstrap.h 한 곳이라 같이 고쳤다.
    inline void RegisterClassInitalize()
    {
        TypeCaster::GetInstance();
        EnumRegistry::GetInstance();
        Registry::GetInstance();
        FactoryRegistry::GetInstance();
    }

    inline void RegisterClassFinalize()
    {
        TypeCaster::Destroy();
        EnumRegistry::Destroy();
        Registry::Destroy();
        FactoryRegistry::Destroy();
    }
}