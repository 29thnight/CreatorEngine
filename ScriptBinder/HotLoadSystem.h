#pragma once
#ifndef DYNAMICCPP_EXPORTS
#include "Core.Minimal.h"
#include "GameObject.h"
#include "EngineSetting.h"

class ModuleBehavior;
class GameObject;
class SceneManager;
class PhysicsManager;
class GameObjectPool;
class AniBehaviour;
class PhysicX;
namespace BT
{
	class NodeFactory;
	class ActionNode;
	class ConditionNode;
	class ConditionDecoratorNode;
}
#pragma region DLLFunctionPtr
//Object Pool 관련 함수 포인터 정의
typedef void (*SetObjectAllocFunc)(Singleton<GameObjectPool>::FGetInstance);

// 모듈 스크립트 관련 함수 포인터 정의
typedef ModuleBehavior* (*ModuleBehaviorFunc)(const char*);
typedef void (*ModuleBehaviorDeleteFunc)(ModuleBehavior* behavior);
typedef const char** (*GetScriptNamesFunc)(int*);

// 행동 트리 노드 관련 함수 포인터 정의
typedef BT::ActionNode* (*BTActionNodeFunc)(const char*);
typedef void (*BTActionNodeDeleteFunc)(BT::ActionNode* actionNode);
typedef BT::ConditionNode* (*BTConditionNodeFunc)(const char*);
typedef void (*BTConditionNodeDeleteFunc)(BT::ConditionNode* conditionNode);
typedef BT::ConditionDecoratorNode* (*BTConditionDecoratorNodeFunc)(const char*);
typedef void (*BTConditionDecoratorNodeDeleteFunc)(BT::ConditionDecoratorNode* conditionNode);
typedef const char** (*ListBTActionNodeNamesFunc)(int*);
typedef const char** (*ListBTConditionNodeNamesFunc)(int*);
typedef const char** (*ListBTConditionDecoratorNodeNamesFunc)(int*);

//에니메이션 FSM 관련 함수 포인터 정의
typedef AniBehaviour* (*AniBehaviourFunc)(const char*);
typedef void (*AniBehaviourDeleteFunc)(AniBehaviour* aniBehaviour);
typedef const char** (*ListAniBehaviourNamesFunc)(int*);

// 씬 매니저와 행동 트리 노드 팩토리 업데이트 함수 포인터 정의[deprecated]
typedef void (*SetSceneManagerFunc)(Singleton<SceneManager>::FGetInstance);
typedef void (*SetBTNodeFactoryFunc)(Singleton<BT::NodeFactory>::FGetInstance);
typedef void (*SetPhysicsManagerFunc)(Singleton<PhysicsManager>::FGetInstance);
typedef void (*SetPhysxFunc)(Singleton<PhysicX>::FGetInstance);
#pragma endregion

class HotLoadSystem : public Singleton<HotLoadSystem>
{
private:
	friend Singleton;

	HotLoadSystem() = default;
	~HotLoadSystem() = default;

public:
	void Initialize();
	void Shutdown();
	bool IsScriptUpToDate();
	void ReloadDynamicLibrary();
	void ReplaceScriptComponent();
	void CompileEvent();
	// 스크립트 생성
	void CreateScriptFile(const std::string_view& name);
	void BindScriptEvents(ModuleBehavior* script, const std::string_view& name);
	void UnbindScriptEvents(ModuleBehavior* script, const std::string_view& name);
	void RegisterScriptReflection(const std::string_view& name, ModuleBehavior* script);
	void UnRegisterScriptReflection(const std::string_view& name);
	// 행동 트리 노드 스크립트 생성
	void CreateActionNodeScript(const std::string_view& name);
	void CreateConditionNodeScript(const std::string_view& name);
	void CreateConditionDecoratorNodeScript(const std::string_view& name);

	void CreateAniBehaviourScript(const std::string_view& name);

#pragma region Script Build Helper
	ModuleBehavior* CreateMonoBehavior(const char* name) const
	{
		if (!m_scriptFactoryFunc) return nullptr;

		return m_scriptFactoryFunc(name);
	}

	void DestroyMonoBehavior(ModuleBehavior* script) const
	{
		if (!m_scriptDeleteFunc) return;

		m_scriptDeleteFunc(script);
	}

	void CollectScriptComponent(GameObject* gameObject, size_t index, const std::string& name)
	{
		std::unique_lock lock(m_scriptFileMutex);
		m_scriptComponentIndexs.emplace_back(gameObject, index, name);
	}

	void UnCollectScriptComponent(GameObject* gameObject, size_t index, const std::string& name)
	{
		std::unique_lock lock(m_scriptFileMutex);
		std::erase_if(m_scriptComponentIndexs, [&](const auto& tuple)
		{
			return std::get<0>(tuple) == gameObject && std::get<2>(tuple) == name;
		});
	}

	bool IsScriptExists(const std::string_view& name) const
	{
		return std::ranges::find(m_scriptNames, name) != m_scriptNames.end();
	}

	std::vector<std::string>& GetScriptNames()
	{
		return m_scriptNames;
	}

	bool IsCompileEventInvoked() const
	{
		return m_isCompileEventInvoked;
	}

	void SetCompileEventInvoked(bool value)
	{
		m_isCompileEventInvoked = value;
	}

	void SetReload(bool value)
	{
		m_isReloading = value;
	}
#pragma endregion

#pragma region BT Build Helper
	BT::ActionNode* CreateActionNode(const char* name) const
	{
		if (!m_btActionNodeFunc) return nullptr;

		return m_btActionNodeFunc(name);
	}

	void DestroyActionNode(BT::ActionNode* actionNode) const
	{
		if (!m_btActionNodeDeleteFunc) return;
		m_btActionNodeDeleteFunc(actionNode);
	}

	BT::ConditionNode* CreateConditionNode(const char* name) const
	{
		if (!m_btConditionNodeFunc) return nullptr;
		return m_btConditionNodeFunc(name);
	}

	void DestroyConditionNode(BT::ConditionNode* conditionNode) const
	{
		if (!m_btConditionNodeDeleteFunc) return;
		m_btConditionNodeDeleteFunc(conditionNode);
	}

	BT::ConditionDecoratorNode* CreateConditionDecoratorNode(const char* name) const
	{
		if (!m_btConditionDecoratorNodeFunc) return nullptr;
		return m_btConditionDecoratorNodeFunc(name);
	}

	void DestroyConditionDecoratorNode(BT::ConditionDecoratorNode* conditionNode) const
	{
		if (!m_btConditionDecoratorNodeDeleteFunc) return;
		m_btConditionDecoratorNodeDeleteFunc(conditionNode);
	}

	const char** ListBTActionNodeNames(int* count) const
	{
		if (!m_listBTActionNodeNamesFunc) return nullptr;
		return m_listBTActionNodeNamesFunc(count);
	}

	const char** ListBTConditionNodeNames(int* count) const
	{
		if (!m_listBTConditionNodeNamesFunc) return nullptr;
		return m_listBTConditionNodeNamesFunc(count);
	}

	const char** ListBTConditionDecoratorNodeNames(int* count) const
	{
		if (!m_listBTConditionDecoratorNodeNamesFunc) return nullptr;
		return m_listBTConditionDecoratorNodeNamesFunc(count);
	}
#pragma endregion


private:
	void Compile();

private:
	HMODULE hDll{};
	ModuleBehaviorFunc			m_scriptFactoryFunc{};
	ModuleBehaviorDeleteFunc	m_scriptDeleteFunc{};
	GetScriptNamesFunc			m_scriptNamesFunc{};

	std::wstring msbuildPath{};
	std::wstring command{};
	std::wstring rebuildCommand{};
	std::atomic_bool m_isStartUp{ false };

private:
	BTActionNodeFunc						m_btActionNodeFunc{};
	BTActionNodeDeleteFunc					m_btActionNodeDeleteFunc{};
	BTConditionNodeFunc						m_btConditionNodeFunc{};
	BTConditionNodeDeleteFunc				m_btConditionNodeDeleteFunc{};
	BTConditionDecoratorNodeFunc			m_btConditionDecoratorNodeFunc{};
	BTConditionDecoratorNodeDeleteFunc		m_btConditionDecoratorNodeDeleteFunc{};
	ListBTActionNodeNamesFunc				m_listBTActionNodeNamesFunc{};
	ListBTConditionNodeNamesFunc			m_listBTConditionNodeNamesFunc{};
	ListBTConditionDecoratorNodeNamesFunc	m_listBTConditionDecoratorNodeNamesFunc{};

private:
	AniBehaviourFunc 				m_aniBehaviourFunc{};
	AniBehaviourDeleteFunc			m_aniBehaviourDeleteFunc{};
	ListAniBehaviourNamesFunc		m_listAniBehaviourNamesFunc{};

private:
	using ModuleBehaviorIndexVector = std::vector<std::tuple<GameObject*, size_t, std::string>>;
	using ModuleBehaviorMetaVector	= std::vector<std::tuple<GameObject*, size_t, MetaYml::Node>>;

	std::vector<std::string>	m_scriptNames{};
	ModuleBehaviorIndexVector	m_scriptComponentIndexs{};
	ModuleBehaviorMetaVector	m_scriptComponentMetaIndexs{};
	std::thread					m_scriptFileThread{};
	std::mutex					m_scriptFileMutex{};
	std::atomic_bool			m_isReloading{ false };
	std::atomic_bool			m_isCompileEventInvoked{ false };
	file::file_time_type		m_lastWriteFileTime{};
};

static auto& ScriptManager = HotLoadSystem::GetInstance();
#endif // !DYNAMICCPP_EXPORTS