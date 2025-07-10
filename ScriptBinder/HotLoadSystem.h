#pragma once
#ifndef DYNAMICCPP_EXPORTS
#include "Core.Minimal.h"
#include "GameObject.h"
#include "EngineSetting.h"

class ModuleBehavior;
class GameObject;
class SceneManager;
namespace BT
{
	class NodeFactory;
}
#pragma region DLLFunctionPtr
typedef ModuleBehavior* (*ModuleBehaviorFunc)(const char*);
typedef const char** (*GetScriptNamesFunc)(int*);
typedef void (*SetSceneManagerFunc)(Singleton<SceneManager>::FGetInstance);
typedef void (*SetBTNodeFactoryFunc)(Singleton<BT::NodeFactory>::FGetInstance);
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

	ModuleBehavior* CreateMonoBehavior(const char* name) const
	{
		if (!m_scriptFactoryFunc) return nullptr;

		return m_scriptFactoryFunc(name);
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

private:
	void Compile();

private:
	HMODULE hDll{};
	ModuleBehaviorFunc m_scriptFactoryFunc{};
	GetScriptNamesFunc m_scriptNamesFunc{};
	SetSceneManagerFunc m_setSceneManagerFunc{};
	SetBTNodeFactoryFunc m_setBTNodeFactoryFunc{};
	std::wstring msbuildPath{ EngineSettingInstance->GetMsbuildPath() };
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


private:
	std::vector<std::string> m_scriptNames{};
	std::vector<std::tuple<GameObject*, size_t, std::string>> m_scriptComponentIndexs{};
	std::vector<std::tuple<GameObject*, size_t, MetaYml::Node>> m_scriptComponentMetaIndexs{};
	std::thread m_scriptFileThread{};
	std::mutex m_scriptFileMutex{};
	std::atomic_bool m_isReloading{ false };
	std::atomic_bool m_isCompileEventInvoked{ false };
	file::file_time_type m_lastWriteFileTime{};
};

static auto& ScriptManager = HotLoadSystem::GetInstance();
#endif // !DYNAMICCPP_EXPORTS