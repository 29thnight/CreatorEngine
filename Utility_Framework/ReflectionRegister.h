#pragma once
#include "ClassProperty.h"
#include "ReflectionType.h"
#include "MetaStateCommand.h"
#include <functional>
#include <any>
#include <typeindex>
#include <stack>
#include <yaml-cpp/yaml.h>

class ComponentFactory;
namespace Meta
{
    // --- TypeCaster: ��Ÿ�� Ÿ�� -> void* ��ȯ ---
    using AnyCaster = std::function<void* (const std::any&)>;

    class TypeCaster : public Singleton<TypeCaster>
    {
    private:
		TypeCaster() = default;
		~TypeCaster() = default;
        friend Singleton;
    public:

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

        template<typename T>
        void RegisterMakeAny()
        {
            _makeAny[typeid(T*)] = [](void* ptr) -> std::any {
                return static_cast<T*>(ptr);
                };

            _makeAny[typeid(std::shared_ptr<T>)] = [](void* ptr) -> std::any {
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

            return {}; // ��ȯ ����
        }

    private:
        std::unordered_map<std::type_index, AnyCaster> _casters;
        std::unordered_map<std::type_index, std::function<std::any(void*)>> _makeAny;
    };

    static inline auto& TypeCast = TypeCaster::GetInstance();

    class Registry : public Singleton<Registry>
    {
    private:
		Registry() = default;
		~Registry() = default;
        friend Singleton;
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
    private:
		EnumRegistry() = default;
		~EnumRegistry() = default;
		friend Singleton;
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

    static inline auto& MetaEnumRegistry = EnumRegistry::GetInstance();
    using FactoryFunction = std::function<void*()>;

    class FactoryRegistry : public Singleton<FactoryRegistry>
    {
    private:
        friend Singleton;
		FactoryRegistry() = default;
		~FactoryRegistry() = default;

    public:
        template<typename T>
        void Register()
        {
            _factories[ToString<T>()] = []() -> T*
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

    private:
        std::unordered_map<std::string, FactoryFunction> _factories;
    };

    static inline auto& MetaFactoryRegistry = FactoryRegistry::GetInstance();

	class UndoManager : public Singleton<UndoManager>
	{
	private:
		UndoManager() = default;
		~UndoManager() = default;
		friend Singleton;

	public:
		void Execute(std::unique_ptr<IUndoableCommand> cmd)
		{
			if (false == m_isGameMode)
            {
                cmd->Redo();
                m_undoStack.push(std::move(cmd));
                while (!m_redoStack.empty()) m_redoStack.pop(); // Redo stack �ʱ�ȭ
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

	static inline auto& UndoCommandManager = UndoManager::GetInstance();

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
			MetaDataRegistry->Register(type.name, type);
			TypeCast->RegisterSharedPtr<T>();
			TypeCast->Register<T>();
		}
	};
}