#pragma once
#include "ClassProperty.h"
#include "ReflectionType.h"
#include "MetaStateCommand.h"
#include "ManagedHeapObject.h"
#include "DLLAcrossSingleton.h"
#include <functional>
#include <any>
#include <typeindex>
#include <stack>
#include <unordered_set>

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
        template<typename T>
        void RegisterSharedPtr()
        {
            const std::type_index casterKey{ typeid(std::shared_ptr<T>) };
            _casters[casterKey] = [](const std::any& a) -> void*
            {
                const auto& sp = std::any_cast<std::shared_ptr<T>>(a);
                return const_cast<void*>(static_cast<const void*>(sp.get()));  //  raw pointer 
            };

            TrackScopeCaster(casterKey);
            TrackScopeMakeAny(casterKey);
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

            TrackScopeCaster(casterKey);
        }

        template<typename T>
        void RegisterMakeAny()
        {
            const std::type_index ptrKey{ typeid(T*) };
            _makeAny[ptrKey] = [](void* ptr) -> std::any {
                return static_cast<T*>(ptr);
                };

            TrackScopeMakeAny(ptrKey);

            const std::type_index sharedKey{ typeid(std::shared_ptr<T>) };
            _makeAny[sharedKey] = [](void* ptr) -> std::any {
                return std::shared_ptr<T>(static_cast<T*>(ptr));
                };

            TrackScopeMakeAny(sharedKey);
        }

        void BeginScope(std::string_view scope)
        {
            _scopeStack.emplace_back(scope);
        }

        void EndScope()
        {
            if (!_scopeStack.empty())
            {
                _scopeStack.pop_back();
            }
        }

        void UnRegisterScope(std::string_view scope)
        {
            const std::string scopeKey(scope);

            if (auto casterIt = _scopeCasterKeys.find(scopeKey); casterIt != _scopeCasterKeys.end())
            {
                for (const auto& key : casterIt->second)
                {
                    ReleaseCaster(key);
                }
                _scopeCasterKeys.erase(casterIt);
            }

            if (auto makeAnyIt = _scopeMakeAnyKeys.find(scopeKey); makeAnyIt != _scopeMakeAnyKeys.end())
            {
                for (const auto& key : makeAnyIt->second)
                {
                    ReleaseMakeAny(key);
                }
                _scopeMakeAnyKeys.erase(makeAnyIt);
            }
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

        void UnRegister(std::string_view name)
        {
            auto nit = _nameToType.find(std::string(name));
            if (nit == _nameToType.end()) return;

            const auto ti = nit->second;
            _casters.erase(ti);
            _makeAny.erase(ti);
            _nameToType.erase(nit);

            return;
        }

    private:
        void TrackScopeCaster(const std::type_index& key)
        {
            if (_scopeStack.empty()) return;

            auto& scopeName = _scopeStack.back();
            auto& typeSet = _scopeCasterKeys[scopeName];
            if (typeSet.insert(key).second)
            {
                ++_casterRefCounts[key];
            }
        }

        void TrackScopeMakeAny(const std::type_index& key)
        {
            if (_scopeStack.empty()) return;

            auto& scopeName = _scopeStack.back();
            auto& typeSet = _scopeMakeAnyKeys[scopeName];
            if (typeSet.insert(key).second)
            {
                ++_makeAnyRefCounts[key];
            }
        }

        void ReleaseCaster(const std::type_index& key)
        {
            if (auto it = _casterRefCounts.find(key); it != _casterRefCounts.end())
            {
                if (--(it->second) == 0)
                {
                    _casterRefCounts.erase(it);
                    _casters.erase(key);
                }
            }
            else
            {
                _casters.erase(key);
            }
        }

        void ReleaseMakeAny(const std::type_index& key)
        {
            if (auto it = _makeAnyRefCounts.find(key); it != _makeAnyRefCounts.end())
            {
                if (--(it->second) == 0)
                {
                    _makeAnyRefCounts.erase(it);
                    _makeAny.erase(key);
                }
            }
            else
            {
                _makeAny.erase(key);
            }
        }

    private:
        std::unordered_map<std::type_index, AnyCaster> _casters;
        std::unordered_map<std::type_index, std::function<std::any(void*)>> _makeAny;
        std::unordered_map<std::string, std::type_index> _nameToType;
        std::vector<std::string> _scopeStack;
        std::unordered_map<std::string, std::unordered_set<std::type_index>> _scopeCasterKeys;
        std::unordered_map<std::string, std::unordered_set<std::type_index>> _scopeMakeAnyKeys;
        std::unordered_map<std::type_index, std::size_t> _casterRefCounts;
        std::unordered_map<std::type_index, std::size_t> _makeAnyRefCounts;
    };

    static auto TypeCast = TypeCaster::GetInstance();

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

        //[warning] 스크립트에서 타입을 등록할 때 사용
        void ScriptRegister(const std::string& name, const Type& type)
        {
            // 스크립트가 리로드되면 기존 타입을 덮어쓰지 않음
            auto it = map.find(name);
            if (it != map.end())
            {
                it->second = type; // 기존 타입 업데이트
            }
            else
            {
                map[name] = type; // 새 타입 등록
            }
        }

        //[warning] 스크립트에서 타입을 등록해제할 때 사용
        void UnRegister(const std::string& name)
        {
            auto it = map.find(name);
            HashedGuid typeID{};
            if (it != map.end())
            {
                typeID = it->second.typeID;
                map.erase(it);
            }

            auto hit = hashMap.find(typeID);
            if (hit != hashMap.end())
            {
                hashMap.erase(hit);
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

    static auto MetaDataRegistry = Registry::GetInstance();

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

    static auto MetaEnumRegistry = EnumRegistry::GetInstance();
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

    static auto MetaFactoryRegistry = FactoryRegistry::GetInstance();

    class UndoManager : public DLLCore::Singleton<UndoManager>
    {
    private:
        UndoManager() = default;
        ~UndoManager() = default;
        friend DLLCore::Singleton<UndoManager>;

    public:
        void Execute(std::unique_ptr<IUndoableCommand> cmd)
        {
            if (false == m_isGameMode)
            {
                cmd->Redo();
                m_undoStack.push(std::move(cmd));
                while (!m_redoStack.empty()) m_redoStack.pop(); // Redo stack 초기화
            }
            else
            {
                cmd->Redo();
                m_gameModeUndoStack.push(std::move(cmd));
                while (!m_gameModeRedoStack.empty()) m_gameModeRedoStack.pop();
            }
        }

        void Undo()
        {
            if (false == m_isGameMode)
            {
                if (m_undoStack.empty()) return;
                auto cmd = std::move(m_undoStack.top());
                m_undoStack.pop();
                cmd->Undo();
                m_redoStack.push(std::move(cmd));
            }
            else
            {
                if (m_gameModeUndoStack.empty()) return;
                auto cmd = std::move(m_gameModeUndoStack.top());
                m_gameModeUndoStack.pop();
                cmd->Undo();
                m_gameModeRedoStack.push(std::move(cmd));
            }
        }

        void Redo()
        {
            if (false == m_isGameMode)
            {
                if (m_redoStack.empty()) return;
                auto cmd = std::move(m_redoStack.top());
                m_redoStack.pop();
                cmd->Redo();
                m_undoStack.push(std::move(cmd));
            }
            else
            {
                if (m_gameModeRedoStack.empty()) return;
                auto cmd = std::move(m_gameModeRedoStack.top());
                m_gameModeRedoStack.pop();
                cmd->Redo();
                m_gameModeUndoStack.push(std::move(cmd));
            }
        }

        void Clear()
        {
            while (!m_undoStack.empty()) m_undoStack.pop();
            while (!m_redoStack.empty()) m_redoStack.pop();
        }

        void ClearGameMode()
        {
            while (!m_gameModeUndoStack.empty()) m_gameModeUndoStack.pop();
            while (!m_gameModeRedoStack.empty()) m_gameModeRedoStack.pop();
        }

        bool m_isGameMode = false;

    private:
        std::stack<std::unique_ptr<IUndoableCommand>> m_undoStack;
        std::stack<std::unique_ptr<IUndoableCommand>> m_redoStack;

        std::stack<std::unique_ptr<IUndoableCommand>> m_gameModeUndoStack;
        std::stack<std::unique_ptr<IUndoableCommand>> m_gameModeRedoStack;
    };

    static auto UndoCommandManager = UndoManager::GetInstance();

    template <typename Enum>
    struct EnumAutoRegistrar
    {
        EnumAutoRegistrar()
        {
            auto enumType = create_enum_type<Enum>();
            EnumRegistry::GetInstance()->Register(enumType.name, enumType);
        }
    };

    template <typename T>
    struct ClassAutoRegistrar
    {
        ClassAutoRegistrar()
        {
            auto type = T::Reflect();
            Registry::GetInstance()->Register(type.name, type);
            TypeCaster::GetInstance()->RegisterSharedPtr<T>();
            TypeCaster::GetInstance()->Register<T>();
        }
    };

    inline void RegisterClassInitalize()
    {
        TypeCaster::GetInstance();
        EnumRegistry::GetInstance();
        Registry::GetInstance();
        FactoryRegistry::GetInstance();
        UndoManager::GetInstance();
    }

    inline void RegisterClassFinalize()
    {
        TypeCaster::Destroy();
        EnumRegistry::Destroy();
        Registry::Destroy();
        FactoryRegistry::Destroy();
        UndoManager::Destroy();
    }
}