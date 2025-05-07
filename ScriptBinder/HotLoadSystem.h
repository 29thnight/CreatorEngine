#pragma once
#include "Core.Minimal.h"
#include "EngineSetting.h"

class ModuleBehavior;
class GameObject;
typedef void (*InitModuleFunc)();
typedef ModuleBehavior* (*ModuleBehaviorFunc)(const char*);
typedef const char** (*GetScriptNamesFunc)(int*);
typedef void (*SetSceneManagerFunc)(void*);

class HotLoadSystem : public Singleton<HotLoadSystem>
{
private:
	friend Singleton;

	HotLoadSystem() = default;
	~HotLoadSystem() = default;

public:
	void Initialize();
	void Shutdown();
	void TrackScriptChanges();
	void ReloadDynamicLibrary();
	void ReplaceScriptComponent();
	void CompileEvent();

	void BindScriptEvents(ModuleBehavior* script, const std::string_view& name);
	void UnbindScriptEvents(ModuleBehavior* script, const std::string_view& name);
	void CreateScriptFile(const std::string_view& name);
	void UpdateSceneManager(void* sceneManager)
	{
		m_setSceneManagerFunc(sceneManager);
	}

	ModuleBehavior* CreateMonoBehavior(const char* name) const
	{
		return m_scriptFactoryFunc(name);
	}

	void CollectScriptComponent(GameObject* gameObject, size_t index, const std::string& name)
	{
		m_scriptComponentIndexs.emplace_back(gameObject, index, name);
	}

	std::vector<std::string>& GetScriptNames()
	{
		return m_scriptNames;
	}

private:
	void Compile();

private:
	HMODULE hDll{};
	ModuleBehaviorFunc m_scriptFactoryFunc{};
	InitModuleFunc m_initModuleFunc{};
	GetScriptNamesFunc m_scriptNamesFunc{};
	SetSceneManagerFunc m_setSceneManagerFunc{};
	std::wstring msbuildPath{ EngineSettingInstance->GetMsbuildPath() };
	std::wstring command{};

#pragma region Script File String
	std::string scriptIncludeString
	{
		"#pragma once\n"
		"#include \"Core.Minimal.h\"\n"
		"#include \"ModuleBehavior.h\"\n"
		"\n"
		"class "
	};

	std::string scriptBodyString
	{
		" : public ModuleBehavior\n"
		"{\n"
		"public:\n"
		"	"
		"	virtual void Start() override;\n"
		"	virtual void FixedUpdate(float fixedTick) override;\n"
		"	virtual void OnTriggerEnter(const Collision& collision) override;\n"
		"	virtual void OnTriggerStay(const Collision& collision) override;\n"
		"	virtual void OnTriggerExit(const Collision& collision) override;\n"
		"	virtual void OnCollisionEnter(const Collision& collision) override;\n"
		"	virtual void OnCollisionStay(const Collision& collision) override;\n"
		"	virtual void OnCollisionExit(const Collision& collision) override;\n"
		"	virtual void Update(float tick) override;\n"
		"	virtual void LateUpdate(float tick) override;\n"
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

	std::string scriptCppEndFixedUpdateString
	{
		"::FixedUpdate(float fixedTick)\n"
		"{\n"
		"}\n"
		"\n"
		"void "
	};

	std::string scriptCppEndOnTriggerEnterString
	{
		"::OnTriggerEnter(const Collision& collision)\n"
		"{\n"
		"}\n"
		"\n"
		"void "
	};

	std::string scriptCppEndOnTriggerStayString
	{
		"::OnTriggerStay(const Collision& collision)\n"
		"{\n"
		"}\n"
		"\n"
		"void "
	};

	std::string scriptCppEndOnTriggerExitString
	{
		"::OnTriggerExit(const Collision& collision)\n"
		"{\n"
		"}\n"
		"\n"
		"void "
	};

	std::string scriptCppEndOnCollisionEnterString
	{
		"::OnCollisionEnter(const Collision& collision)\n"
		"{\n"
		"}\n"
		"\n"
		"void "
	};

	std::string scriptCppEndOnCollisionStayString
	{
		"::OnCollisionStay(const Collision& collision)\n"
		"{\n"
		"}\n"
		"\n"
		"void "
	};

	std::string scriptCppEndOnCollisionExitString
	{
		"::OnCollisionExit(const Collision& collision)\n"
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
		"void "
	};

	std::string scriptCppEndLateUpdateString
	{
		"::LateUpdate(float tick)\n"
		"{\n"
		"}\n"
	};

	std::string scriptFactoryIncludeString
	{
		"#include \""
	};

	std::string scriptFactoryFunctionString
	{
		"	CreateFactory::GetInstance()->RegisterFactory(\""
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
	
	std::vector<std::string> m_scriptNames{};
	std::vector<std::tuple<GameObject*, size_t, std::string>> m_scriptComponentIndexs{};
	std::thread m_scriptFileThread{};
	std::atomic_bool m_isReloading{ false };
	std::atomic_bool m_isCompileEventInvoked{ false };
	file::file_time_type m_lastWriteFileTime{};
};

static auto& ScriptManager = HotLoadSystem::GetInstance();
