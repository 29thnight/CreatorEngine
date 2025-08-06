#pragma once
#ifndef DYNAMICCPP_EXPORTS
#include "Core.Minimal.h"
#include "GameObject.h"
#include "EngineSetting.h"

class ModuleBehavior;
class GameObject;
class AnimationState;
class SceneManager;
class PhysicsManager;
class GameObjectPool;
class AniBehavior;
class PhysicX;
class Scene;
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
typedef AniBehavior* (*AniBehaviorFunc)(const char*);
typedef void (*AniBehaviorDeleteFunc)(AniBehavior* AniBehavior);
typedef const char** (*ListAniBehaviorNamesFunc)(int*);

// 씬 매니저와 행동 트리 노드 팩토리 업데이트 함수 포인터 정의[deprecated]
typedef void (*SetSceneManagerFunc)(Singleton<SceneManager>::FGetInstance);
typedef void (*SetBTNodeFactoryFunc)(Singleton<BT::NodeFactory>::FGetInstance);
typedef void (*SetPhysicsManagerFunc)(Singleton<PhysicsManager>::FGetInstance);
typedef void (*SetPhysxFunc)(Singleton<PhysicX>::FGetInstance);
#pragma endregion

class HotLoadSystem : public DLLCore::Singleton<HotLoadSystem>
{
private:
	friend DLLCore::Singleton<HotLoadSystem>;

	HotLoadSystem() = default;
	~HotLoadSystem() = default;

public:
	void Initialize();
	void Shutdown();
	bool IsScriptUpToDate();
	void ReloadDynamicLibrary();
	void ReplaceScriptComponent();
	void ReplaceScriptComponentTargetScene(Scene* targetScene);
	void CompileEvent();
	// 스크립트 생성
	void CreateScriptFile(std::string_view name);
	void BindScriptEvents(ModuleBehavior* script, std::string_view name);
	void UnbindScriptEvents(ModuleBehavior* script, std::string_view name);
	void RegisterScriptReflection(std::string_view name, ModuleBehavior* script);
	void UnRegisterScriptReflection(std::string_view name);
	// 행동 트리 노드 스크립트 생성
	void CreateActionNodeScript(std::string_view name);
	void CreateConditionNodeScript(std::string_view name);
	void CreateConditionDecoratorNodeScript(std::string_view name);
	// 에니메이션 FSM 스크립트 생성
	void CreateAniBehaviorScript(std::string_view name);

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

	bool IsScriptExists(std::string_view name) const
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

#pragma region Ani Build Helper
	AniBehavior* CreateAniBehavior(const char* name) const
	{
		if (!m_AniBehaviorFunc) return nullptr;
		return m_AniBehaviorFunc(name);
	}

	void DestroyAniBehavior(AniBehavior* aniBehavior) const
	{
		if (!m_AniBehaviorDeleteFunc) return;
		m_AniBehaviorDeleteFunc(aniBehavior);
	}

	const char** ListAniBehaviorNames(int* count) const
	{
		if (!m_listAniBehaviorNamesFunc) return nullptr;
		return m_listAniBehaviorNamesFunc(count);
	}

	void CollectAniBehavior(AnimationState* aniState)
	{
		std::unique_lock lock(m_scriptFileMutex);
		m_aniBehaviorStates.push_back(aniState);
	}

	void UnCollectAniBehavior(AnimationState* aniState)
	{
		std::unique_lock lock(m_scriptFileMutex);
		std::erase_if(m_aniBehaviorStates, [&](const auto& state)
		{
			return state == aniState;
		});
	}

	bool IsAniBehaviorExists(std::string_view name) const
	{
		return std::ranges::find(m_aniBehaviorNames, name) != m_aniBehaviorNames.end();
	}

	void ResetAniBehaviorPtr();

	std::vector<std::string>& GetAniBehaviorNames()
	{
		return m_aniBehaviorNames;
	}
#pragma endregion

private:
	void Compile();

private:
	// DLL 함수 포인터
	HMODULE hDll{};
private:
	// Script 관련 함수 포인터
	ModuleBehaviorFunc						m_scriptFactoryFunc{};
	ModuleBehaviorDeleteFunc				m_scriptDeleteFunc{};
	GetScriptNamesFunc						m_scriptNamesFunc{};
	// msbuild 관련 명령어 및 초기화 변수
	std::wstring							msbuildPath{};
	std::wstring							command{};
	std::wstring							rebuildCommand{};
	std::atomic_bool						m_isStartUp{ false };

private:
	// 행동 트리 노드 관련 함수 포인터
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
	// 에니메이션 행동 스크립트 관련 함수 포인터
	AniBehaviorFunc 						m_AniBehaviorFunc{};
	AniBehaviorDeleteFunc					m_AniBehaviorDeleteFunc{};
	ListAniBehaviorNamesFunc				m_listAniBehaviorNamesFunc{};

private:
	using ModuleBehaviorIndexVector			= std::vector<std::tuple<GameObject*, size_t, std::string>>;
	using ModuleBehaviorMetaVector			= std::vector<std::tuple<GameObject*, size_t, MetaYml::Node>>;
	using AniBehaviorVector					= std::vector<AnimationState*>;
	
	// 스크립트 컴포넌트 인덱스와 메타 정보 저장
	std::vector<std::string>				m_scriptNames{};
	ModuleBehaviorIndexVector				m_scriptComponentIndexs{};
	ModuleBehaviorMetaVector				m_scriptComponentMetaIndexs{};

	// 에니메이션 행동 스크립트 이름 저장
	std::vector<std::string>				m_aniBehaviorNames{};
	AniBehaviorVector 						m_aniBehaviorStates{};

	// 스크립트 빌드 스레드 제어 변수
	std::thread								m_scriptFileThread{};
	std::mutex								m_scriptFileMutex{};
	std::atomic_bool						m_isReloading{ false };
	std::atomic_bool						m_isCompileEventInvoked{ false };
	file::file_time_type					m_lastWriteFileTime{};
};

static auto ScriptManager = HotLoadSystem::GetInstance();
#endif // !DYNAMICCPP_EXPORTS