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

// 씬 매니저와 행동 트리 노드 팩토리 업데이트 함수 포인터 정의
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

	void BindScriptEvents(ModuleBehavior* script, const std::string_view& name);
	void UnbindScriptEvents(ModuleBehavior* script, const std::string_view& name);
	void CreateScriptFile(const std::string_view& name);
	void RegisterScriptReflection(const std::string_view& name, ModuleBehavior* script);
	void UnRegisterScriptReflection(const std::string_view& name);

	void CreateActionNodeScript(const std::string_view& name);
	void CreateConditionNodeScript(const std::string_view& name);
	void CreateConditionDecoratorNodeScript(const std::string_view& name);

	void UpdateObjectAllocFunc(Singleton<GameObjectPool>::FGetInstance objectPool)
	{
		if (!m_setObjectAllocFunc) return;

		m_setObjectAllocFunc(objectPool);
	}

#pragma region Script Build Helper
	void UpdateSceneManager(Singleton<SceneManager>::FGetInstance sceneManager)
	{
		if (!m_setSceneManagerFunc) return;

		m_setSceneManagerFunc(sceneManager);
	}

	void UpdateBTNodeFactory(Singleton<BT::NodeFactory>::FGetInstance btNodeFactory)
	{
		if (!m_setBTNodeFactoryFunc) return;

		m_setBTNodeFactoryFunc(btNodeFactory);
	}

	void UpdatePhysicsManager(Singleton<PhysicsManager>::FGetInstance physicsManager) 
	{
		if (!m_setPhysicsManagerFunc) return;

		m_setPhysicsManagerFunc(physicsManager);
	}

	void UpdatePhysx(Singleton<PhysicX>::FGetInstance physx) {
		if (!m_setPhysxFunc) return;

		m_setPhysxFunc(physx);
	}

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
	SetObjectAllocFunc m_setObjectAllocFunc{};
	ModuleBehaviorFunc m_scriptFactoryFunc{};
	ModuleBehaviorDeleteFunc m_scriptDeleteFunc{};
	GetScriptNamesFunc m_scriptNamesFunc{};
	SetSceneManagerFunc m_setSceneManagerFunc{};
	SetBTNodeFactoryFunc m_setBTNodeFactoryFunc{};
	SetPhysicsManagerFunc m_setPhysicsManagerFunc{};
	SetPhysxFunc m_setPhysxFunc{};

	std::wstring msbuildPath{};
	std::wstring command{};
	std::wstring rebuildCommand{};
	std::atomic_bool m_isStartUp{ false };

private:
#pragma region Script File String
	std::string scriptIncludeString
	{
		"#pragma once\n"
		"#include \"Core.Minimal.h\"\n"
		"#include \"ModuleBehavior.h\"\n"
		"\n"
		"class "
	};

	std::string scriptInheritString
	{
		" : public ModuleBehavior\n"
		"{\n"
		"public:\n"
		"	MODULE_BEHAVIOR_BODY("
	};

	std::string scriptBodyString
	{
		")\n"
		"	virtual void Awake() override {}\n"
		"	virtual void Start() override;\n"
		"	virtual void FixedUpdate(float fixedTick) override {}\n"
		"	virtual void OnTriggerEnter(const Collision& collision) override {}\n"
		"	virtual void OnTriggerStay(const Collision& collision) override {}\n"
		"	virtual void OnTriggerExit(const Collision& collision) override {}\n"
		"	virtual void OnCollisionEnter(const Collision& collision) override {}\n"
		"	virtual void OnCollisionStay(const Collision& collision) override {}\n"
		"	virtual void OnCollisionExit(const Collision& collision) override {}\n"
		"	virtual void Update(float tick) override;\n"
		"	virtual void LateUpdate(float tick) override {}\n"
		"	virtual void OnDisable() override  {}\n"
		"	virtual void OnDestroy() override  {}\n"
	};

	std::string scriptEndString
	{
		"};\n"
	};

	std::string scriptCppString
	{
		"#include \""
	};

	std::string scriptCppEndString
	{
		".h\"\n"
		"#include \"pch.h\""
		"\n"
		"void "
	};

	std::string scriptCppEndBodyString
	{
		"::Start()\n"
		"{\n"
		"}\n"
		"\n"
		"void "
	};

	std::string scriptCppEndUpdateString
	{
		"::Update(float tick)\n"
		"{\n"
		"}\n"
		"\n"
	};

	std::string scriptFactoryIncludeString
	{
		"#include \""
	};

	std::string scriptFactoryFunctionString
	{
		"		CreateFactory::GetInstance()->RegisterFactory(\""
	};

	std::string scriptFactoryFunctionLambdaString
	{
		"\", []() { return new "
	};

	std::string scriptFactoryFunctionEndString
	{
		"(); });\n"
	};

	std::string markerFactoryHeaderString
	{
		"// Automation include ScriptClass header"
	};

	std::string markerFactoryFuncString
	{
		"// Register the factory function for TestBehavior Automation"
	};
#pragma endregion
	
private:
	BTActionNodeFunc				m_btActionNodeFunc{};
	BTActionNodeDeleteFunc			m_btActionNodeDeleteFunc{};
	BTConditionNodeFunc				m_btConditionNodeFunc{};
	BTConditionNodeDeleteFunc		m_btConditionNodeDeleteFunc{};
	BTConditionDecoratorNodeFunc	m_btConditionDecoratorNodeFunc{};
	BTConditionDecoratorNodeDeleteFunc m_btConditionDecoratorNodeDeleteFunc{};
	ListBTActionNodeNamesFunc		m_listBTActionNodeNamesFunc{};
	ListBTConditionNodeNamesFunc	m_listBTConditionNodeNamesFunc{};
	ListBTConditionDecoratorNodeNamesFunc m_listBTConditionDecoratorNodeNamesFunc{};

private:
#pragma region Action and Condition Node String
	std::string actionNodeIncludeString
	{
		"#include \"Core.Minimal.h\"\n"
		"#include \"BTHeader.h\"\n"
		"\n"
		"using namespace BT;\n"
		"\n"
		"class "
	};

	std::string actionNodeInheritString
	{
		" : public ActionNode\n"
		"{\n"
		"public:\n"
		"	BT_ACTION_BODY("
	};

	std::string actionNodeEndString
	{
		")\n"
		"	virtual NodeStatus Tick(float deltatime, BlackBoard& blackBoard) override;\n"
		"};\n"
	};

	std::string actionNodeCPPString
	{
		"#include \""
	};

	std::string actionNodeCPPEndString
	{
		".h\"\n"
		"#include \"pch.h\"\n"
		"\n"
		"NodeStatus "
	};

	std::string actionNodeCPPEndBodyString
	{
		"::Tick(float deltatime, BlackBoard& blackBoard)\n"
		"{\n"
		"	return NodeStatus::Success;\n"
		"}\n"
	};

	std::string conditionNodeIncludeString
	{
		"#include \"Core.Minimal.h\"\n"
		"#include \"BTHeader.h\"\n"
		"\n"
		"using namespace BT;\n"
		"\n"
		"class "
	};

	std::string conditionNodeInheritString
	{
		" : public ConditionNode\n"
		"{\n"
		"public:\n"
		"	BT_CONDITION_BODY("
	};

	std::string conditionNodeEndString
	{
		")\n"
		"	virtual bool ConditionCheck(float deltatime, const BlackBoard& blackBoard) override;\n"
		"};\n"
	};

	std::string conditionNodeCPPString
	{
		"#include \""
	};

	std::string conditionNodeCPPEndString
	{
		".h\"\n"
		"#include \"pch.h\"\n"
		"\n"
		"bool "
	};

	std::string conditionNodeCPPEndBodyString
	{
		"::ConditionCheck(float deltatime, const BlackBoard& blackBoard)\n"
		"{\n"
		"	return false;\n"
		"}\n"
	};

	std::string conditionDecoratorNodeIncludeString
	{
		"#include \"Core.Minimal.h\"\n"
		"#include \"BTHeader.h\"\n"
		"\n"
		"using namespace BT;\n"
		"\n"
		"class "
	};

	std::string conditionDecoratorNodeInheritString
	{
		" : public ConditionDecoratorNode\n"
		"{\n"
		"public:\n"
		"	BT_CONDITIONDECORATOR_BODY("
	};

	std::string conditionDecoratorNodeEndString
	{
		")\n"
		"	virtual bool ConditionCheck(float deltatime, const BlackBoard& blackBoard) override;\n"
		"};\n"
	};

	std::string conditionDecoratorNodeCPPString
	{
		"#include \""
	};

	std::string conditionDecoratorNodeCPPEndString
	{
		".h\"\n"
		"#include \"pch.h\"\n"
		"\n"
		"bool "
	};

	std::string conditionDecoratorNodeCPPEndBodyString
	{
		"::ConditionCheck(float deltatime, const BlackBoard& blackBoard)\n"
		"{\n"
		"	return false;\n"
		"}\n"
	};

	std::string actionFactoryIncludeString
	{
		"#include \""
	};

	std::string actionFactoryFunctionString
	{
		"	ActionCreateFactory::GetInstance()->RegisterFactory(\""
	};

	std::string actionFactoryFunctionLambdaString
	{
		"\", []() { return new "
	};

	std::string actionFactoryFunctionEndString
	{
		"(); });\n"
	};

	std::string conditionFactoryIncludeString
	{
		"#include \""
	};

	std::string conditionFactoryFunctionString
	{
		"	ConditionCreateFactory::GetInstance()->RegisterFactory(\""
	};

	std::string conditionFactoryFunctionLambdaString
	{
		"\", []() { return new "
	};

	std::string conditionFactoryFunctionEndString
	{
		"(); });\n"
	};

	std::string conditionDecoratorFactoryIncludeString
	{
		"#include \""
	};

	std::string conditionDecoratorFactoryFunctionString
	{
		"	ConditionDecoratorCreateFactory::GetInstance()->RegisterFactory(\""
	};

	std::string conditionDecoratorFactoryFunctionLambdaString
	{
		"\", []() { return new "
	};

	std::string conditionDecoratorFactoryFunctionEndString
	{
		"(); });\n"
	};

	std::string markerActionFactoryHeaderString
	{
		"// Automation include ActionNodeClass header"
	};

	std::string markerActionFactoryFuncString
	{
		"// Register the factory function for BTAction Automation"
	};

	std::string markerConditionFactoryHeaderString
	{
		"// Automation include ConditionNodeClass header"
	};

	std::string markerConditionFactoryFuncString
	{
		"// Register the factory function for BTCondition Automation"
	};

	std::string markerConditionDecoratorFactoryHeaderString
	{
		"// Automation include ConditionDecoratorNodeClass header"
	};

	std::string markerConditionDecoratorFactoryFuncString
	{
		"// Register the factory function for BTConditionDecorator Automation"
	};

#pragma endregion

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