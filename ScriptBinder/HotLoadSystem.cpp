#ifndef DYNAMICCPP_EXPORTS
#include "HotLoadSystem.h"
#include "LogSystem.h"
#include "GameObject.h"
#include "SceneManager.h"
#include "ModuleBehavior.h"
#include "pugixml.hpp"
#include "ReflectionYml.h"
#include "ProgressWindow.h"
#include "ReflectionRegister.h"
#include "AIManager.h"
#include "DLLRefHelper.h"
#include "StringHelper.h"
#include "ScriptStringModule.h"
#include "AnimationState.h"
#include "MSBuildHelper.h"

#pragma comment(linker,"\"/manifestdependency:type='win32' \
name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

constexpr int MAX__COMPILE_ITERATION = 4;

void HotLoadSystem::Initialize()
{
    std::wstring slnPath = PathFinder::DynamicSolutionPath("Dynamic_CPP.sln").wstring();
	msbuildPath = EngineSettingInstance->GetMsbuildPath();
	if (msbuildPath.empty())
	{
		Debug->LogError("MSBuild path is not set. Please check your Visual Studio installation.");
		return;
	}

#if defined(_DEBUG)
	command = std::wstring(L"cmd /c \"")
        + L"\"" + msbuildPath + L"\" "
        + L"\"" + slnPath + L"\" "
        + L"/m /t:Build /p:Configuration=Debug /p:Platform=x64 /nologo"
        + L"\"";
#else
	command = std::wstring(L"cmd /c \"")
		+ L"\"" + msbuildPath + L"\" "
		+ L"\"" + slnPath + L"\" "
		+ L"/m /t:Build /p:Configuration=Release /p:Platform=x64 /nologo"
		+ L"\"";
#endif

	{
		try
		{
			Compile();
		}
		catch (const std::exception& e)
		{
			Debug->LogError("Failed to compile script: " + std::string(e.what()));
			return;
		}
	}

	const char** scriptNames = nullptr;
	int scriptCount = 0;
	scriptNames = m_scriptNamesFunc(&scriptCount);

	for (int i = 0; i < scriptCount; ++i)
	{
		std::string scriptName = scriptNames[i];
		m_scriptNames.push_back(scriptName);
	}

	for (auto& scriptName : m_scriptNames)
	{
		auto tempPtr = std::shared_ptr<ModuleBehavior>(CreateMonoBehavior(scriptName.c_str()));
		if (nullptr == tempPtr)
		{
			Debug->LogError("Failed to create script: " + scriptName);
			continue;
		}
		RegisterScriptReflection(scriptName, tempPtr.get());
	}

	AIManagers->InitalizeBehaviorTreeSystem();

	const char** aniBehaviorNames = nullptr;
	int aniBehaviorCount = 0;
	aniBehaviorNames = m_listAniBehaviorNamesFunc(&aniBehaviorCount);
	for(int i = 0; i < aniBehaviorCount; ++i)
	{
		std::string aniBehaviorName = aniBehaviorNames[i];
		m_aniBehaviorNames.push_back(aniBehaviorName);
	}
}

void HotLoadSystem::Shutdown()
{
	for(auto& scriptName : m_scriptNames)
	{
		UnRegisterScriptReflection(scriptName);
	}

	Meta::UndoCommandManager->Clear();
	Meta::UndoCommandManager->ClearGameMode();

	if (hDll)
	{
		FreeLibrary(hDll);
		hDll = nullptr;
	}
}

bool HotLoadSystem::IsScriptUpToDate()
{
	if (!m_isStartUp)
	{
		m_isStartUp = true;
	}

	file::path dllPath = PathFinder::RelativeToExecutable("Dynamic_CPP.dll");
	file::path funcMainPath = PathFinder::DynamicSolutionPath("funcMain.h");
	file::path slnPath = PathFinder::DynamicSolutionPath("Dynamic_CPP.sln");

	if (!file::exists(dllPath))
		return false; // DLL 파일이 존재하지 않으면 빌드 필요

	auto dllTimeStamp = file::last_write_time(dllPath);
	auto funcMainTimeStamp = file::last_write_time(funcMainPath);

	if (funcMainTimeStamp > dllTimeStamp) return false; // 빌드 필요함

	for (const auto& scriptName : m_scriptNames)
	{
		file::path scriptPath = PathFinder::Relative("Script\\" + scriptName + ".cpp");

		if (file::exists(scriptPath))
		{
			auto scriptTimeStamp = file::last_write_time(scriptPath);
			if (scriptTimeStamp > dllTimeStamp) return false; // 빌드 필요함 (스크립트가 더 최신임)
		}
	}

	return true; // 모든 스크립트가 DLL보다 오래됨 (업투데이트됨)
}

void HotLoadSystem::ReloadDynamicLibrary()
{
#ifndef BUILD_FLAG
	if(true == m_isCompileEventInvoked)
	{
		g_progressWindow->Launch();
		g_progressWindow->SetStatusText(L"Compile Script Library...");
		g_progressWindow->SetProgress(100);

		try
		{
			Compile();
		}
		catch (const std::exception& e)
		{
			Debug->LogError("Failed to compile script: " + std::string(e.what()));
			m_isCompileEventInvoked = false;
			g_progressWindow->Close();
			return;
		}

		m_scriptNames.clear();
		const char** scriptNames = nullptr;
		int scriptCount = 0;
		scriptNames = m_scriptNamesFunc(&scriptCount);

		AIManagers->InitalizeBehaviorTreeSystem();

		for (int i = 0; i < scriptCount; ++i)
		{
			std::string scriptName = scriptNames[i];
			std::string scriptFileName = std::string(scriptName);
			m_scriptNames.push_back(scriptName);
		}

		for (auto& scriptName : m_scriptNames)
		{
			auto tempPtr = std::shared_ptr<ModuleBehavior>(CreateMonoBehavior(scriptName.c_str()));
			if (nullptr == tempPtr)
			{
				Debug->LogError("Failed to create script: " + scriptName);
				continue;
			}
			file::path scriptPath = PathFinder::Relative("Script\\" + scriptName + ".cpp");
			DataSystems->ForceCreateYamlMetaFile(scriptPath);
			RegisterScriptReflection(scriptName, tempPtr.get());
		}

		std::unordered_set<std::string> validNames(m_scriptNames.begin(), m_scriptNames.end());

		for (auto& [gameObject, index, name] : m_scriptComponentIndexs)
		{
			if (!validNames.count(name))
			{
				std::cout << "Invalid Script : " << name << " (GameObject index: " << index << ")\n";
				gameObject = nullptr; // GameObject도 nullptr로 설정
			}
		}

		std::erase_if(m_scriptComponentIndexs, [](const auto& tuple)
		{
			return std::get<0>(tuple) == nullptr; // GameObject가 nullptr인 경우 제거
		});

		ReplaceScriptComponent();

		const char** aniBehaviorNames = nullptr;
		int aniBehaviorCount = 0;
		aniBehaviorNames = m_listAniBehaviorNamesFunc(&aniBehaviorCount);

		// 1. 새로운 이름들을 set으로 구성
		std::unordered_set<std::string> newAniBehaviorNames;
		for (int i = 0; i < aniBehaviorCount; ++i)
		{
			newAniBehaviorNames.insert(aniBehaviorNames[i]);
		}

		// 2. 기존 이름 중 새로 받은 목록에 없는 것만 삭제 대상으로 추가
		std::unordered_set<std::string> deleteTargetAniBehaviorNames;
		for (const std::string& oldName : m_aniBehaviorNames)
		{
			if (!newAniBehaviorNames.count(oldName))
			{
				deleteTargetAniBehaviorNames.insert(oldName);
				Debug->LogError("Invalid AniBehavior (deleted): " + oldName);
			}
		}

		// 3. 기존 이름 목록 갱신 (중복 가능성 주의)
		m_aniBehaviorNames.clear();
		m_aniBehaviorNames.insert(m_aniBehaviorNames.end(), newAniBehaviorNames.begin(), newAniBehaviorNames.end());

		for (auto& aniState : m_aniBehaviorStates)
		{
			if (!aniState) continue;

			if(deleteTargetAniBehaviorNames.count(aniState->behaviourName) > 0)
			{
				Debug->LogError("Deleting AniBehavior: " + aniState->behaviourName);
				aniState->ClearBehaviour();
				continue;
			}
			else
			{
				aniState->SetBehaviour(aniState->behaviourName, true);
			}
		}

		g_progressWindow->Close();
	}
#endif
}

void HotLoadSystem::ReplaceScriptComponent()
{
	if (true == m_isReloading && false == m_isCompileEventInvoked)
	{
		auto activeScene = SceneManagers->GetActiveScene();
		activeScene->m_selectedSceneObject = nullptr;

		auto& gameObjects = activeScene->m_SceneObjects;
		std::unordered_set<GameObject*> gameObjectSet;

		for(auto& gameObject : gameObjects)
			gameObjectSet.insert(gameObject.get());

		for (auto& [gameObject, index, name] : m_scriptComponentIndexs)
		{
			if (!gameObjectSet.contains(gameObject)) continue; // 게임 오브젝트가 씬에 존재하지 않으면 스킵

			auto* newScript = CreateMonoBehavior(name.c_str());
			if (nullptr == newScript)
			{
				Debug->LogError("Failed to create script: " + std::string(name));
				continue;
			}

			if(SceneManagers->m_isGameStart)
			{
				ScriptManager->BindScriptEvents(newScript, name);
			}

			newScript->SetOwner(gameObject);
			auto sharedScript = std::shared_ptr<Component>(newScript);
			if(index >= gameObject->m_components.size())
			{
				gameObject->m_components.push_back(sharedScript);
				size_t backIndex = gameObject->m_components.size() - 1;
				gameObject->m_componentIds[newScript->m_scriptTypeID] = backIndex;

				void* scriptPtr = reinterpret_cast<void*>(gameObject->m_components[backIndex].get());
				const auto& scriptType = newScript->ScriptReflect();

				for (auto& [_gameObject, idx, node] : m_scriptComponentMetaIndexs)
				{
					if (_gameObject == gameObject && index == idx)
					{
						Meta::Deserialize(scriptPtr, scriptType, node);
						break;
					}
				}
			}
			else
			{
				if(nullptr != gameObject->m_components[index])
				{
					auto node = Meta::Serialize(gameObject->m_components[index].get());

					gameObject->m_components[index].swap(sharedScript);
					void* scriptPtr = reinterpret_cast<void*>(gameObject->m_components[index].get());
					const auto& scriptType = newScript->ScriptReflect();

					Meta::Deserialize(scriptPtr, scriptType, node);
				}
				else
				{
					gameObject->m_components[index].swap(sharedScript);
					void* scriptPtr = reinterpret_cast<void*>(gameObject->m_components[index].get());
					const auto& scriptType = newScript->ScriptReflect();

					for (auto& [gameObject, idx, node] : m_scriptComponentMetaIndexs)
					{
						if (gameObject == gameObject && index == idx)
						{
							Meta::Deserialize(scriptPtr, scriptType, node);
							break;
						}
					}
				}


			}
			newScript->m_scriptGuid = DataSystems->GetFilenameToGuid(name + ".cpp");
		}

		m_scriptComponentIndexs.clear();

		m_isReloading = false;
	}
}

void HotLoadSystem::ReplaceScriptComponentTargetScene(Scene* targetScene)
{
	auto activeScene = SceneManagers->GetActiveScene();
	activeScene->m_selectedSceneObject = nullptr;

	auto& gameObjects = activeScene->m_SceneObjects;
	std::unordered_set<GameObject*> gameObjectSet;

	for (auto& gameObject : gameObjects)
		gameObjectSet.insert(gameObject.get());

	for (auto& [gameObject, index, name] : m_scriptComponentIndexs)
	{
		if (!gameObjectSet.contains(gameObject)) continue; // 게임 오브젝트가 씬에 존재하지 않으면 스킵

		auto* newScript = CreateMonoBehavior(name.c_str());
		if (nullptr == newScript)
		{
			Debug->LogError("Failed to create script: " + std::string(name));
			continue;
		}

		if (SceneManagers->m_isGameStart)
		{
			ScriptManager->BindScriptEvents(newScript, name);
		}

		newScript->SetOwner(gameObject);
		auto sharedScript = std::shared_ptr<Component>(newScript);
		if (index >= gameObject->m_components.size())
		{
			gameObject->m_components.push_back(sharedScript);
			size_t backIndex = gameObject->m_components.size() - 1;
			gameObject->m_componentIds[newScript->m_scriptTypeID] = backIndex;

			void* scriptPtr = reinterpret_cast<void*>(gameObject->m_components[backIndex].get());
			const auto& scriptType = newScript->ScriptReflect();

			for (auto& [gameObject, idx, node] : m_scriptComponentMetaIndexs)
			{
				if (gameObject == gameObject && index == idx)
				{
					Meta::Deserialize(scriptPtr, scriptType, node);
					break;
				}
			}
		}
		else
		{
			if (nullptr != gameObject->m_components[index])
			{
				auto node = Meta::Serialize(gameObject->m_components[index].get());

				gameObject->m_components[index].swap(sharedScript);
				void* scriptPtr = reinterpret_cast<void*>(gameObject->m_components[index].get());
				const auto& scriptType = newScript->ScriptReflect();

				Meta::Deserialize(scriptPtr, scriptType, node);
			}
			else
			{
				gameObject->m_components[index].swap(sharedScript);
				void* scriptPtr = reinterpret_cast<void*>(gameObject->m_components[index].get());
				const auto& scriptType = newScript->ScriptReflect();

				for (auto& [gameObject, idx, node] : m_scriptComponentMetaIndexs)
				{
					if (gameObject == gameObject && index == idx)
					{
						Meta::Deserialize(scriptPtr, scriptType, node);
						break;
					}
				}
			}


		}
		newScript->m_scriptGuid = DataSystems->GetFilenameToGuid(name + ".cpp");
	}

	m_scriptComponentIndexs.clear();
}

void HotLoadSystem::CompileEvent()
{
	m_isCompileEventInvoked = true;
}

void HotLoadSystem::BindScriptEvents(ModuleBehavior* script, std::string_view name)
{
	auto activeScene = SceneManagers->GetActiveScene();
	//결국 이렇게되면 .meta파일을 클라이언트가 가지고 있어야 됨...
	std::string scriptMetaFile = "Assets\\Script\\" + std::string(name) + ".cpp" + ".meta";
	file::path scriptMetaFileName = PathFinder::DynamicSolutionPath(scriptMetaFile);

	if (file::exists(scriptMetaFileName))
	{
		bool haveScriptReflectionAttribute{ false };
		MetaYml::Node scriptNode = MetaYml::LoadFile(scriptMetaFileName.string());

		std::vector<std::string> events;
		if (scriptNode["eventRegisterSetting"])
		{
			for (const auto& node : scriptNode["eventRegisterSetting"])
			{
				events.push_back(node.as<std::string>());
			}

			for (const auto& event : events)
			{
				if (event == "Awake")
				{
					if (script->m_awakeEventHandle.IsValid()) continue;

					script->m_awakeEventHandle = activeScene->AwakeEvent.AddRaw(script, &ModuleBehavior::AwakeInvoke);
				}
				else if (event == "OnEnable")
				{
					if (script->m_onEnableEventHandle.IsValid()) continue;

					script->m_onEnableEventHandle = activeScene->OnEnableEvent.AddRaw(script, &ModuleBehavior::OnEnableInvoke);
				}
				else if (event == "Start")
				{
					if (script->m_startEventHandle.IsValid()) continue;

					script->m_startEventHandle = activeScene->StartEvent.AddRaw(script, &ModuleBehavior::StartInvoke);
				}
				else if (event == "FixedUpdate")
				{
					if (script->m_fixedUpdateEventHandle.IsValid()) continue;

					script->m_fixedUpdateEventHandle = activeScene->FixedUpdateEvent.AddRaw(script, &ModuleBehavior::FixedUpdateInvoke);
				}
				else if (event == "OnTriggerEnter")
				{
					if (script->m_onTriggerEnterEventHandle.IsValid()) continue;

					script->m_onTriggerEnterEventHandle = activeScene->OnTriggerEnterEvent.AddRaw(script, &ModuleBehavior::OnTriggerEnterInvoke);
				}
				else if (event == "OnTriggerStay")
				{
					if (script->m_onTriggerStayEventHandle.IsValid()) continue;

					script->m_onTriggerStayEventHandle = activeScene->OnTriggerStayEvent.AddRaw(script, &ModuleBehavior::OnTriggerStayInvoke);
				}
				else if (event == "OnTriggerExit")
				{
					if (script->m_onTriggerExitEventHandle.IsValid()) continue;

					script->m_onTriggerExitEventHandle = activeScene->OnTriggerExitEvent.AddRaw(script, &ModuleBehavior::OnTriggerExitInvoke);
				}
				else if (event == "OnCollisionEnter")
				{
					if (script->m_onCollisionEnterEventHandle.IsValid()) continue;

					script->m_onCollisionEnterEventHandle = activeScene->OnCollisionEnterEvent.AddRaw(script, &ModuleBehavior::OnCollisionEnterInvoke);
				}
				else if (event == "OnCollisionStay")
				{
					if (script->m_onCollisionStayEventHandle.IsValid()) continue;

					script->m_onCollisionStayEventHandle = activeScene->OnCollisionStayEvent.AddRaw(script, &ModuleBehavior::OnCollisionStayInvoke);
				}
				else if (event == "OnCollisionExit")
				{
					if (script->m_onCollisionExitEventHandle.IsValid()) continue;

					script->m_onCollisionExitEventHandle = activeScene->OnCollisionExitEvent.AddRaw(script, &ModuleBehavior::OnCollisionExitInvoke);
				}
				else if (event == "Update")
				{
					if (script->m_updateEventHandle.IsValid()) continue;

					script->m_updateEventHandle = activeScene->UpdateEvent.AddRaw(script, &ModuleBehavior::UpdateInvoke);
				}
				else if (event == "LateUpdate")
				{
					if (script->m_lateUpdateEventHandle.IsValid()) continue;

					script->m_lateUpdateEventHandle = activeScene->LateUpdateEvent.AddRaw(script, &ModuleBehavior::LateUpdateInvoke);
				}
				else if (event == "OnDisable")
				{
					if (script->m_onDisableEventHandle.IsValid()) continue;

					script->m_onDisableEventHandle = activeScene->OnDisableEvent.AddRaw(script, &ModuleBehavior::OnDisableInvoke);
				}
				else if (event == "OnDestroy")
				{
					if (script->m_onDestroyEventHandle.IsValid()) continue;

					script->m_onDestroyEventHandle = activeScene->OnDestroyEvent.AddRaw(script, &ModuleBehavior::OnDestroyInvoke);
				}
			}
			
		}
	}
}

void HotLoadSystem::UnbindScriptEvents(ModuleBehavior* script, std::string_view name)
{
	auto activeScene = SceneManagers->GetActiveScene();

	if (script->m_awakeEventHandle.IsValid())
	{
		activeScene->AwakeEvent -= script->m_awakeEventHandle;
	}

	if (script->m_onEnableEventHandle.IsValid())
	{
		activeScene->OnEnableEvent -= script->m_onEnableEventHandle;
	}

	if (script->m_startEventHandle.IsValid())
	{
		activeScene->StartEvent -= script->m_startEventHandle;
	}

	if (script->m_fixedUpdateEventHandle.IsValid())
	{
		activeScene->FixedUpdateEvent -= script->m_fixedUpdateEventHandle;
	}

	if (script->m_onTriggerEnterEventHandle.IsValid())
	{
		activeScene->OnTriggerEnterEvent -= script->m_onTriggerEnterEventHandle;
	}

	if (script->m_onTriggerStayEventHandle.IsValid())
	{
		activeScene->OnTriggerStayEvent -= script->m_onTriggerStayEventHandle;
	}

	if (script->m_onTriggerExitEventHandle.IsValid())
	{
		activeScene->OnTriggerExitEvent -= script->m_onTriggerExitEventHandle;
	}

	if (script->m_onCollisionEnterEventHandle.IsValid())
	{
		activeScene->OnCollisionEnterEvent -= script->m_onCollisionEnterEventHandle;
	}

	if (script->m_onCollisionStayEventHandle.IsValid())
	{
		activeScene->OnCollisionStayEvent -= script->m_onCollisionStayEventHandle;
	}

	if (script->m_onCollisionExitEventHandle.IsValid())
	{
		activeScene->OnCollisionExitEvent -= script->m_onCollisionExitEventHandle;
	}

	if (script->m_updateEventHandle.IsValid())
	{
		activeScene->UpdateEvent -= script->m_updateEventHandle;
	}

	if (script->m_lateUpdateEventHandle.IsValid())
	{
		activeScene->LateUpdateEvent -= script->m_lateUpdateEventHandle;
	}

	if (script->m_onDisableEventHandle.IsValid())
	{
		activeScene->OnDisableEvent -= script->m_onDisableEventHandle;
	}

	if (script->m_onDestroyEventHandle.IsValid())
	{
		activeScene->OnDestroyEvent -= script->m_onDestroyEventHandle;
	}

}

void HotLoadSystem::CreateScriptFile(std::string_view name)
{
#ifndef BUILD_FLAG
	std::string scriptHeaderFileName = std::string(name) + ".h";
	std::string scriptBodyFileName = std::string(name) + ".cpp";
	std::string scriptHeaderFilePath = PathFinder::Relative("Script\\" + scriptHeaderFileName).string();
	std::string scriptBodyFilePath = PathFinder::Relative("Script\\" + scriptBodyFileName).string();
	std::string scriptFactoryPath = PathFinder::DynamicSolutionPath("CreateFactory.h").string();
	std::string scriptFactoryFuncPath = PathFinder::DynamicSolutionPath("funcMain.h").string();
	std::string scriptProjPath = PathFinder::DynamicSolutionPath("Dynamic_CPP.vcxproj").string();
	std::string scriptFilterPath = PathFinder::DynamicSolutionPath("Dynamic_CPP.vcxproj.filters").string();
	//Create Script File
	std::ofstream scriptFile(scriptHeaderFilePath);
	if (scriptFile.is_open())
	{
		scriptFile 
			<< scriptIncludeString 
			<< name 
			<< scriptInheritString
			<< name
			<< scriptBodyString
			<< scriptEndString;

		scriptFile.close();
	}
	else
	{
		throw std::runtime_error("Failed to create script file");
	}

	std::ofstream scriptBodyFile(scriptBodyFilePath);
	if (scriptBodyFile.is_open())
	{
		scriptBodyFile
			<< scriptCppString
			<< name
			<< scriptCppEndString
			<< name
			<< scriptCppEndBodyString
			<< name
			<< scriptCppEndUpdateString;
		scriptBodyFile.close();
	}
	else
	{
		throw std::runtime_error("Failed to create script file");
	}
	//Factory Add
	std::ifstream scriptFactoryFile(scriptFactoryPath);
	if (scriptFactoryFile.is_open())
	{
		std::stringstream buffer;
		buffer << scriptFactoryFile.rdbuf();
		std::string content = buffer.str();
		scriptFactoryFile.close();

		size_t posHeader = content.find(markerFactoryHeaderString);
		if (posHeader != std::string::npos)
		{
			size_t endLine = content.find('\n', posHeader);
			if (endLine != std::string::npos)
			{
				content.insert(endLine + 1, scriptFactoryIncludeString.data() + scriptHeaderFileName + "\"\n");
			}
		}
		else
		{
			throw std::runtime_error("Failed to find marker in script factory file");
		}

		std::ofstream scriptFactoryFileOut(scriptFactoryPath);
		if (scriptFactoryFileOut.is_open())
		{
			scriptFactoryFileOut << content;
			scriptFactoryFileOut.close();
		}
		else
		{
			throw std::runtime_error("Failed to create script factory file");
		}
	}
	else
	{
		throw std::runtime_error("Failed to create script factory file");
	}

	//Factory Func Add
	std::ifstream scriptFactoryFuncFile(scriptFactoryFuncPath);
	if (scriptFactoryFuncFile.is_open())
	{
		std::stringstream buffer;
		buffer << scriptFactoryFuncFile.rdbuf();
		std::string content = buffer.str();
		scriptFactoryFuncFile.close();

		size_t posFunc = content.find(markerFactoryFuncString);
		if (posFunc != std::string::npos)
		{
			size_t endLine = content.find('\n', posFunc);
			if (endLine != std::string::npos)
			{
				content.insert(endLine + 1, scriptFactoryFunctionString.data() + std::string(name) +scriptFactoryFunctionLambdaString.data() + std::string(name) + scriptFactoryFunctionEndString.data());
			}
		}
		else
		{
			throw std::runtime_error("Failed to find marker in script factory file");
		}

		std::ofstream scriptFactoryFuncFileOut(scriptFactoryFuncPath);
		if (scriptFactoryFuncFileOut.is_open())
		{
			scriptFactoryFuncFileOut << content;
			scriptFactoryFuncFileOut.close();
		}
		else
		{
			throw std::runtime_error("Failed to create script factory file");
		}
	}
	else
	{
		throw std::runtime_error("Failed to create script factory file");
	}

	{
		//Filter Add
		pugi::xml_document doc;
		if (!doc.load_file(scriptFilterPath.c_str(), pugi::parse_full, pugi::encoding_auto))
		{
			throw std::runtime_error("Failed to load XML file");
		}

		std::vector<pugi::xml_node> itemGroups;
		for (pugi::xml_node itemGroup = doc.child("Project").child("ItemGroup"); itemGroup; itemGroup = itemGroup.next_sibling("ItemGroup"))
		{
			itemGroups.push_back(itemGroup);
		}

		// 두 번째 ItemGroup (인덱스 1)에 헤더 파일 추가 (ClInclude)
		pugi::xml_node headerGroup = itemGroups[1];
		pugi::xml_node newHeader = headerGroup.append_child("ClInclude");
		newHeader.append_attribute("Include") = "Assets\\Script\\" + scriptHeaderFileName;
		pugi::xml_node filterNodeHeader = newHeader.append_child("Filter");
		filterNodeHeader.text().set("Script\\ScriptClass");

		// 세 번째 ItemGroup (인덱스 2)에 소스 파일 추가 (ClCompile)
		pugi::xml_node cppGroup = itemGroups[2];
		pugi::xml_node newSource = cppGroup.append_child("ClCompile");
		newSource.append_attribute("Include") = "Assets\\Script\\" + scriptBodyFileName;
		pugi::xml_node filterNodeSource = newSource.append_child("Filter");
		filterNodeSource.text().set("Script\\ScriptClass");

		if (!doc.save_file(scriptFilterPath.c_str(), PUGIXML_TEXT("\t"), 1U, pugi::encoding_auto))
		{
			throw std::runtime_error("Failed to save XML file");
		}
	}

	{
		//Proj Add
		pugi::xml_document doc;
		if (!doc.load_file(scriptProjPath.c_str(), pugi::parse_full, pugi::encoding_auto))
		{
			throw std::runtime_error("Failed to load XML file");
		}

		std::vector<pugi::xml_node> itemGroups;
		for (pugi::xml_node itemGroup = doc.child("Project").child("ItemGroup"); itemGroup; itemGroup = itemGroup.next_sibling("ItemGroup"))
		{
			itemGroups.push_back(itemGroup);
		}

		// 두 번째 ItemGroup (인덱스 1)에 헤더 파일 추가 (ClInclude)
		pugi::xml_node headerGroup = itemGroups[1];
		pugi::xml_node newHeader = headerGroup.append_child("ClInclude");
		newHeader.append_attribute("Include") = "Assets\\Script\\" + scriptHeaderFileName;

		// 세 번째 ItemGroup (인덱스 2)에 소스 파일 추가 (ClCompile)
		pugi::xml_node cppGroup = itemGroups[2];
		pugi::xml_node newSource = cppGroup.append_child("ClCompile");
		newSource.append_attribute("Include") = "Assets\\Script\\" + scriptBodyFileName;

		pugi::xml_node additionalOptionsDebug = newSource.append_child("AdditionalOptions");
		additionalOptionsDebug.append_attribute("Condition") = "'$(Configuration)|$(Platform)'=='Debug|x64'";
		additionalOptionsDebug.append_child(pugi::node_pcdata).set_value("/utf-8 %(AdditionalOptions)");

		pugi::xml_node additionalOptionsRelease = newSource.append_child("AdditionalOptions");
		additionalOptionsRelease.append_attribute("Condition") = "'$(Configuration)|$(Platform)'=='Release|x64'";
		additionalOptionsRelease.append_child(pugi::node_pcdata).set_value("/utf-8 %(AdditionalOptions)");

		if (!doc.save_file(scriptProjPath.c_str(), PUGIXML_TEXT("\t"), 1U, pugi::encoding_auto))
		{
			throw std::runtime_error("Failed to save XML file");
		}
	}
#endif // BUILD_FLAG
}

void HotLoadSystem::RegisterScriptReflection(std::string_view name, ModuleBehavior* script)
{
	std::string scriptMetaFile = "Assets\\Script\\" + std::string(name) + ".cpp" + ".meta";
	file::path scriptMetaFileName = PathFinder::DynamicSolutionPath(scriptMetaFile);

	if (file::exists(scriptMetaFileName))
	{
		bool haveScriptReflectionAttribute{ false };
		MetaYml::Node scriptNode = MetaYml::LoadFile(scriptMetaFileName.string());

		if (scriptNode["reflectionFlag"] && !scriptNode["reflectionFlag"].IsNull())
		{
			haveScriptReflectionAttribute = scriptNode["reflectionFlag"].as<bool>();
		}

		if (true == haveScriptReflectionAttribute)
		{
			Meta::Registry::GetInstance()->ScriptRegister(name.data(), script->ScriptReflect());
		}
	}
}

void HotLoadSystem::UnRegisterScriptReflection(std::string_view name)
{
	Meta::Registry::GetInstance()->UnRegister(name.data());
}

void HotLoadSystem::CreateActionNodeScript(std::string_view name)
{
#ifndef BUILD_FLAG
	if (!file::exists(PathFinder::Relative("BehaviorTree")))
	{
		file::create_directories(PathFinder::Relative("BehaviorTree"));
	}

	if (!file::exists(PathFinder::Relative("BehaviorTree\\Action")))
	{
		file::create_directories(PathFinder::Relative("BehaviorTree\\Action"));
	}

	std::string actionHeaderFileName = std::string(name) + ".h";
	std::string actionBodyFileName = std::string(name) + ".cpp";
	std::string actionHeaderFilePath = PathFinder::Relative("BehaviorTree\\Action\\" 
		+ actionHeaderFileName).string();
	std::string actionBodyFilePath = PathFinder::Relative("BehaviorTree\\Action\\" 
		+ actionBodyFileName).string();

	std::string actionFactoryPath = PathFinder::DynamicSolutionPath("BTActionFactory.h").string();
	std::string actionFactoryFuncPath = PathFinder::DynamicSolutionPath("funcMain.h").string();
	std::string actionProjPath = PathFinder::DynamicSolutionPath("Dynamic_CPP.vcxproj").string();
	std::string actionFilterPath = PathFinder::DynamicSolutionPath("Dynamic_CPP.vcxproj.filters").string();

	std::ofstream actionFile(actionHeaderFilePath);
	if (actionFile.is_open())
	{
		actionFile 
			<< actionNodeIncludeString
			<< name 
			<< actionNodeInheritString
			<< name
			<< actionNodeEndString;
		actionFile.close();
	}
	else
	{
		throw std::runtime_error("Failed to create action file");
	}

	std::ofstream actionBodyFile(actionBodyFilePath);
	if (actionBodyFile.is_open())
	{
		actionBodyFile
			<< actionNodeCPPString
			<< name
			<< actionNodeCPPEndString
			<< name
			<< actionNodeCPPEndBodyString;
		actionBodyFile.close();
	}
	else
	{
		throw std::runtime_error("Failed to create action body file");
	}

	//Factory Add
	std::ifstream actionFactoryFile(actionFactoryPath);
	if (actionFactoryFile.is_open())
	{
		std::stringstream buffer;
		buffer << actionFactoryFile.rdbuf();
		std::string content = buffer.str();
		actionFactoryFile.close();
		size_t posHeader = content.find(markerActionFactoryHeaderString);
		if (posHeader != std::string::npos)
		{
			size_t endLine = content.find('\n', posHeader);
			if (endLine != std::string::npos)
			{
				content.insert(endLine + 1, actionFactoryIncludeString.data() + actionHeaderFileName + "\"\n");
			}
		}
		else
		{
			throw std::runtime_error("Failed to find marker in action factory file");
		}
		std::ofstream actionFactoryFileOut(actionFactoryPath);
		if (actionFactoryFileOut.is_open())
		{
			actionFactoryFileOut << content;
			actionFactoryFileOut.close();
		}
		else
		{
			throw std::runtime_error("Failed to create action factory file");
		}
	}
	else
	{
		throw std::runtime_error("Failed to create action factory file");
	}

	//Factory Func Add
	std::ifstream actionFactoryFuncFile(actionFactoryFuncPath);
	if (actionFactoryFuncFile.is_open())
	{
		std::stringstream buffer;
		buffer << actionFactoryFuncFile.rdbuf();
		std::string content = buffer.str();
		actionFactoryFuncFile.close();
		size_t posFunc = content.find(markerActionFactoryFuncString);
		if (posFunc != std::string::npos)
		{
			size_t endLine = content.find('\n', posFunc);
			if (endLine != std::string::npos)
			{
				content.insert(endLine + 1, actionFactoryFunctionString.data() + std::string(name) + actionFactoryFunctionLambdaString.data() + std::string(name) + actionFactoryFunctionEndString.data());
			}
		}
		else
		{
			throw std::runtime_error("Failed to find marker in action factory file");
		}
		std::ofstream actionFactoryFuncFileOut(actionFactoryFuncPath);
		if (actionFactoryFuncFileOut.is_open())
		{
			actionFactoryFuncFileOut << content;
			actionFactoryFuncFileOut.close();
		}
		else
		{
			throw std::runtime_error("Failed to create action factory file");
		}
	}
	else
	{
		throw std::runtime_error("Failed to create action factory file");
	}

	{
		//Filter Add
		pugi::xml_document doc;
		if (!doc.load_file(actionFilterPath.c_str(), pugi::parse_full, pugi::encoding_auto))
		{
			throw std::runtime_error("Failed to load XML file");
		}
		std::vector<pugi::xml_node> itemGroups;
		for (pugi::xml_node itemGroup = doc.child("Project").child("ItemGroup"); itemGroup; itemGroup = itemGroup.next_sibling("ItemGroup"))
		{
			itemGroups.push_back(itemGroup);
		}
		// 두 번째 ItemGroup (인덱스 1)에 헤더 파일 추가 (ClInclude)
		pugi::xml_node headerGroup = itemGroups[1];
		pugi::xml_node newHeader = headerGroup.append_child("ClInclude");
		newHeader.append_attribute("Include") = "Assets\\BehaviorTree\\Action\\" + actionHeaderFileName;
		pugi::xml_node filterNodeHeader = newHeader.append_child("Filter");
		filterNodeHeader.text().set("BehaviorTree\\Action");
		// 세 번째 ItemGroup (인덱스 2)에 소스 파일 추가 (ClCompile)
		pugi::xml_node cppGroup = itemGroups[2];
		pugi::xml_node newSource = cppGroup.append_child("ClCompile");
		newSource.append_attribute("Include") = "Assets\\BehaviorTree\\Action\\" + actionBodyFileName;
		pugi::xml_node filterNodeSource = newSource.append_child("Filter");
		filterNodeSource.text().set("BehaviorTree\\Action");
		if (!doc.save_file(actionFilterPath.c_str(), PUGIXML_TEXT("\t"), 1U, pugi::encoding_auto))
		{
			throw std::runtime_error("Failed to save XML file");
		}
	}
	{
		pugi::xml_document doc;
		if (!doc.load_file(actionProjPath.c_str(), pugi::parse_full, pugi::encoding_auto))
		{
			throw std::runtime_error("Failed to load XML file");
		}
		std::vector<pugi::xml_node> itemGroups;
		for (pugi::xml_node itemGroup = doc.child("Project").child("ItemGroup"); itemGroup; itemGroup = itemGroup.next_sibling("ItemGroup"))
		{
			itemGroups.push_back(itemGroup);
		}

		// 두 번째 ItemGroup (인덱스 1)에 헤더 파일 추가 (ClInclude)
		pugi::xml_node headerGroup = itemGroups[1];
		pugi::xml_node newHeader = headerGroup.append_child("ClInclude");
		newHeader.append_attribute("Include") = "Assets\\BehaviorTree\\Action\\" + actionHeaderFileName;
		// 세 번째 ItemGroup (인덱스 2)에 소스 파일 추가 (ClCompile)
		pugi::xml_node cppGroup = itemGroups[2];
		pugi::xml_node newSource = cppGroup.append_child("ClCompile");
		newSource.append_attribute("Include") = "Assets\\BehaviorTree\\Action\\" + actionBodyFileName;
		pugi::xml_node additionalOptionsDebug = newSource.append_child("AdditionalOptions");
		additionalOptionsDebug.append_attribute("Condition") = "'$(Configuration)|$(Platform)'=='Debug|x64'";
		additionalOptionsDebug.append_child(pugi::node_pcdata).set_value("/utf-8 %(AdditionalOptions)");
		pugi::xml_node additionalOptionsRelease = newSource.append_child("AdditionalOptions");
		additionalOptionsRelease.append_attribute("Condition") = "'$(Configuration)|$(Platform)'=='Release|x64'";
		additionalOptionsRelease.append_child(pugi::node_pcdata).set_value("/utf-8 %(AdditionalOptions)");
		if (!doc.save_file(actionProjPath.c_str(), PUGIXML_TEXT("\t"), 1U, pugi::encoding_auto))
		{
			throw std::runtime_error("Failed to save XML file");
		}
	}
#endif // BUILD_FLAG
}

void HotLoadSystem::CreateConditionNodeScript(std::string_view name)
{
#ifndef BUILD_FLAG
	if (!file::exists(PathFinder::Relative("BehaviorTree")))
	{
		file::create_directories(PathFinder::Relative("BehaviorTree"));
	}

	if (!file::exists(PathFinder::Relative("BehaviorTree\\ConditionDecorator")))
	{
		file::create_directories(PathFinder::Relative("BehaviorTree\\ConditionDecorator"));
	}

	std::string conditionHeaderFileName = std::string(name) + ".h";
	std::string conditionBodyFileName = std::string(name) + ".cpp";
	std::string conditionHeaderFilePath = PathFinder::Relative("BehaviorTree\\Condition\\" 
		+ conditionHeaderFileName).string();
	std::string conditionBodyFilePath = PathFinder::Relative("BehaviorTree\\Condition\\"
		+ conditionBodyFileName).string();

	std::string conditionFactoryPath = PathFinder::DynamicSolutionPath("BTConditionFactory.h").string();
	std::string conditionFactoryFuncPath = PathFinder::DynamicSolutionPath("funcMain.h").string();
	std::string conditionProjPath = PathFinder::DynamicSolutionPath("Dynamic_CPP.vcxproj").string();
	std::string conditionFilterPath = PathFinder::DynamicSolutionPath("Dynamic_CPP.vcxproj.filters").string();

	std::ofstream conditionFile(conditionHeaderFilePath);
	if (conditionFile.is_open())
	{
		conditionFile 
			<< conditionNodeIncludeString
			<< name 
			<< conditionNodeInheritString
			<< name
			<< conditionNodeEndString;
		conditionFile.close();
	}
	else
	{
		throw std::runtime_error("Failed to create condition file");
	}

	std::ofstream conditionBodyFile(conditionBodyFilePath);
	if (conditionBodyFile.is_open())
	{
		conditionBodyFile
			<< conditionNodeCPPString
			<< name
			<< conditionNodeCPPEndString
			<< name
			<< conditionNodeCPPEndBodyString;
		conditionBodyFile.close();
	}
	else
	{
		throw std::runtime_error("Failed to create condition body file");
	}

	//Factory Add
	std::ifstream conditionFactoryFile(conditionFactoryPath);
	if (conditionFactoryFile.is_open())
	{
		std::stringstream buffer;
		buffer << conditionFactoryFile.rdbuf();
		std::string content = buffer.str();
		conditionFactoryFile.close();
		size_t posHeader = content.find(markerConditionFactoryHeaderString);
		if (posHeader != std::string::npos)
		{
			size_t endLine = content.find('\n', posHeader);
			if (endLine != std::string::npos)
			{
				content.insert(endLine + 1, conditionFactoryIncludeString.data() + conditionHeaderFileName + "\"\n");
			}
		}
		else
		{
			throw std::runtime_error("Failed to find marker in condition factory file");
		}
		std::ofstream conditionFactoryFileOut(conditionFactoryPath);
		if (conditionFactoryFileOut.is_open())
		{
			conditionFactoryFileOut << content;
			conditionFactoryFileOut.close();
		}
		else
		{
			throw std::runtime_error("Failed to create condition factory file");
		}
	}
	else
	{
		throw std::runtime_error("Failed to create condition factory file");
	}

	//Factory Func Add
	std::ifstream conditionFactoryFuncFile(conditionFactoryFuncPath);
	if (conditionFactoryFuncFile.is_open())
	{
		std::stringstream buffer;
		buffer << conditionFactoryFuncFile.rdbuf();
		std::string content = buffer.str();
		conditionFactoryFuncFile.close();
		size_t posFunc = content.find(markerConditionFactoryFuncString);
		if (posFunc != std::string::npos)
		{
			size_t endLine = content.find('\n', posFunc);
			if (endLine != std::string::npos)
			{
				content.insert(endLine + 1, conditionFactoryFunctionString.data() + std::string(name) + conditionFactoryFunctionLambdaString.data() + std::string(name) + conditionFactoryFunctionEndString.data());
			}
		}
		else
		{
			throw std::runtime_error("Failed to find marker in condition factory file");
		}
		std::ofstream conditionFactoryFuncFileOut(conditionFactoryFuncPath);
		if (conditionFactoryFuncFileOut.is_open())
		{
			conditionFactoryFuncFileOut << content;
			conditionFactoryFuncFileOut.close();
		}
		else
		{
			throw std::runtime_error("Failed to create condition factory file");
		}
	}
	else
	{
		throw std::runtime_error("Failed to create condition factory file");
	}
	{
		//Filter Add
		pugi::xml_document doc;
		if (!doc.load_file(conditionFilterPath.c_str(), pugi::parse_full, pugi::encoding_auto))
		{
			throw std::runtime_error("Failed to load XML file");
		}
		
		std::vector<pugi::xml_node> itemGroups;
		
		for (pugi::xml_node itemGroup = doc.child("Project").child("ItemGroup"); itemGroup; itemGroup = itemGroup.next_sibling("ItemGroup"))
		{
			itemGroups.push_back(itemGroup);
		}

		// 두 번째 ItemGroup (인덱스 1)에 헤더 파일 추가 (ClInclude)
		pugi::xml_node headerGroup = itemGroups[1];
		pugi::xml_node newHeader = headerGroup.append_child("ClInclude");
		newHeader.append_attribute("Include") = "Assets\\BehaviorTree\\Condition\\" + conditionHeaderFileName;
		pugi::xml_node filterNodeHeader = newHeader.append_child("Filter");
		filterNodeHeader.text().set("BehaviorTree\\Condition");
		// 세 번째 ItemGroup (인덱스 2)에 소스 파일 추가 (ClCompile)
		pugi::xml_node cppGroup = itemGroups[2];
		pugi::xml_node newSource = cppGroup.append_child("ClCompile");
		newSource.append_attribute("Include") = "Assets\\BehaviorTree\\Condition\\" + conditionBodyFileName;
		pugi::xml_node filterNodeSource = newSource.append_child("Filter");
		filterNodeSource.text().set("BehaviorTree\\Condition");
		if (!doc.save_file(conditionFilterPath.c_str(), PUGIXML_TEXT("\t"), 1U, pugi::encoding_auto))
		{
			throw std::runtime_error("Failed to save XML file");
		}
	}
	{
		pugi::xml_document doc;
		if (!doc.load_file(conditionProjPath.c_str(), pugi::parse_full, pugi::encoding_auto))
		{
			throw std::runtime_error("Failed to load XML file");
		}
		
		std::vector<pugi::xml_node> itemGroups;
		
		for (pugi::xml_node itemGroup = doc.child("Project").child("ItemGroup"); itemGroup; itemGroup = itemGroup.next_sibling("ItemGroup"))
		{
			itemGroups.push_back(itemGroup);
		}
		// 두 번째 ItemGroup (인덱스 1)에 헤더 파일 추가 (ClInclude)
		pugi::xml_node headerGroup = itemGroups[1];
		pugi::xml_node newHeader = headerGroup.append_child("ClInclude");
		newHeader.append_attribute("Include") = "Assets\\BehaviorTree\\Condition\\" + conditionHeaderFileName;
		// 세 번째 ItemGroup (인덱스 2)에 소스 파일 추가 (ClCompile)
		pugi::xml_node cppGroup = itemGroups[2];
		pugi::xml_node newSource = cppGroup.append_child("ClCompile");
		newSource.append_attribute("Include") = "Assets\\BehaviorTree\\Condition\\" + conditionBodyFileName;
		pugi::xml_node additionalOptionsDebug = newSource.append_child("AdditionalOptions");
		additionalOptionsDebug.append_attribute("Condition") = "'$(Configuration)|$(Platform)'=='Debug|x64'";
		additionalOptionsDebug.append_child(pugi::node_pcdata).set_value("/utf-8 %(AdditionalOptions)");
		pugi::xml_node additionalOptionsRelease = newSource.append_child("AdditionalOptions");
		additionalOptionsRelease.append_attribute("Condition") = "'$(Configuration)|$(Platform)'=='Release|x64'";
		additionalOptionsRelease.append_child(pugi::node_pcdata).set_value("/utf-8 %(AdditionalOptions)");
		
		if (!doc.save_file(conditionProjPath.c_str(), PUGIXML_TEXT("\t"), 1U, pugi::encoding_auto))
		{
			throw std::runtime_error("Failed to save XML file");
		}
	}
#endif // BUILD_FLAG
}

void HotLoadSystem::CreateConditionDecoratorNodeScript(std::string_view name)
{
#ifndef BUILD_FLAG
	if (!file::exists(PathFinder::Relative("BehaviorTree")))
	{
		file::create_directories(PathFinder::Relative("BehaviorTree"));
	}

	if (!file::exists(PathFinder::Relative("BehaviorTree\\ConditionDecorator")))
	{
		file::create_directories(PathFinder::Relative("BehaviorTree\\ConditionDecorator"));
	}

	std::string conditionHeaderFileName = std::string(name) + ".h";
	std::string conditionBodyFileName = std::string(name) + ".cpp";
	std::string conditionHeaderFilePath = PathFinder::Relative("BehaviorTree\\ConditionDecorator\\"
		+ conditionHeaderFileName).string();
	std::string conditionBodyFilePath = PathFinder::Relative("BehaviorTree\\ConditionDecorator\\"
		+ conditionBodyFileName).string();

	std::string conditionFactoryPath = PathFinder::DynamicSolutionPath("BTConditionDecoratorFactory.h").string();
	std::string conditionFactoryFuncPath = PathFinder::DynamicSolutionPath("funcMain.h").string();
	std::string conditionProjPath = PathFinder::DynamicSolutionPath("Dynamic_CPP.vcxproj").string();
	std::string conditionFilterPath = PathFinder::DynamicSolutionPath("Dynamic_CPP.vcxproj.filters").string();

	std::ofstream conditionFile(conditionHeaderFilePath);
	if (conditionFile.is_open())
	{
		conditionFile
			<< conditionDecoratorNodeIncludeString
			<< name
			<< conditionDecoratorNodeInheritString
			<< name
			<< conditionDecoratorNodeEndString;
		conditionFile.close();
	}
	else
	{
		throw std::runtime_error("Failed to create conditionDecorator file");
	}

	std::ofstream conditionBodyFile(conditionBodyFilePath);
	if (conditionBodyFile.is_open())
	{
		conditionBodyFile
			<< conditionDecoratorNodeCPPString
			<< name
			<< conditionDecoratorNodeCPPEndString
			<< name
			<< conditionDecoratorNodeCPPEndBodyString;
		conditionBodyFile.close();
	}
	else
	{
		throw std::runtime_error("Failed to create condition body file");
	}

	//Factory Add
	std::ifstream conditionFactoryFile(conditionFactoryPath);
	if (conditionFactoryFile.is_open())
	{
		std::stringstream buffer;
		buffer << conditionFactoryFile.rdbuf();
		std::string content = buffer.str();
		conditionFactoryFile.close();
		size_t posHeader = content.find(markerConditionDecoratorFactoryHeaderString);
		if (posHeader != std::string::npos)
		{
			size_t endLine = content.find('\n', posHeader);
			if (endLine != std::string::npos)
			{
				content.insert(endLine + 1, conditionDecoratorFactoryIncludeString.data() + conditionHeaderFileName + "\"\n");
			}
		}
		else
		{
			throw std::runtime_error("Failed to find marker in condition factory file");
		}
		std::ofstream conditionFactoryFileOut(conditionFactoryPath);
		if (conditionFactoryFileOut.is_open())
		{
			conditionFactoryFileOut << content;
			conditionFactoryFileOut.close();
		}
		else
		{
			throw std::runtime_error("Failed to create condition factory file");
		}
	}
	else
	{
		throw std::runtime_error("Failed to create condition factory file");
	}

	//Factory Func Add
	std::ifstream conditionFactoryFuncFile(conditionFactoryFuncPath);
	if (conditionFactoryFuncFile.is_open())
	{
		std::stringstream buffer;
		buffer << conditionFactoryFuncFile.rdbuf();
		std::string content = buffer.str();
		conditionFactoryFuncFile.close();
		size_t posFunc = content.find(markerConditionDecoratorFactoryFuncString);
		if (posFunc != std::string::npos)
		{
			size_t endLine = content.find('\n', posFunc);
			if (endLine != std::string::npos)
			{
				content.insert(endLine + 1, conditionDecoratorFactoryFunctionString.data() + std::string(name) + conditionDecoratorFactoryFunctionLambdaString.data() + std::string(name) + conditionDecoratorFactoryFunctionEndString.data());
			}
		}
		else
		{
			throw std::runtime_error("Failed to find marker in condition factory file");
		}
		std::ofstream conditionFactoryFuncFileOut(conditionFactoryFuncPath);
		if (conditionFactoryFuncFileOut.is_open())
		{
			conditionFactoryFuncFileOut << content;
			conditionFactoryFuncFileOut.close();
		}
		else
		{
			throw std::runtime_error("Failed to create condition factory file");
		}
	}
	else
	{
		throw std::runtime_error("Failed to create condition factory file");
	}
	{
		//Filter Add
		pugi::xml_document doc;
		if (!doc.load_file(conditionFilterPath.c_str(), pugi::parse_full, pugi::encoding_auto))
		{
			throw std::runtime_error("Failed to load XML file");
		}

		std::vector<pugi::xml_node> itemGroups;

		for (pugi::xml_node itemGroup = doc.child("Project").child("ItemGroup"); itemGroup; itemGroup = itemGroup.next_sibling("ItemGroup"))
		{
			itemGroups.push_back(itemGroup);
		}

		// 두 번째 ItemGroup (인덱스 1)에 헤더 파일 추가 (ClInclude)
		pugi::xml_node headerGroup = itemGroups[1];
		pugi::xml_node newHeader = headerGroup.append_child("ClInclude");
		newHeader.append_attribute("Include") = "Assets\\BehaviorTree\\ConditionDecorator\\" + conditionHeaderFileName;
		pugi::xml_node filterNodeHeader = newHeader.append_child("Filter");
		filterNodeHeader.text().set("BehaviorTree\\ConditionDecorator");
		// 세 번째 ItemGroup (인덱스 2)에 소스 파일 추가 (ClCompile)
		pugi::xml_node cppGroup = itemGroups[2];
		pugi::xml_node newSource = cppGroup.append_child("ClCompile");
		newSource.append_attribute("Include") = "Assets\\BehaviorTree\\ConditionDecorator\\" + conditionBodyFileName;
		pugi::xml_node filterNodeSource = newSource.append_child("Filter");
		filterNodeSource.text().set("BehaviorTree\\ConditionDecorator");
		if (!doc.save_file(conditionFilterPath.c_str(), PUGIXML_TEXT("\t"), 1U, pugi::encoding_auto))
		{
			throw std::runtime_error("Failed to save XML file");
		}
	}
	{
		pugi::xml_document doc;
		if (!doc.load_file(conditionProjPath.c_str(), pugi::parse_full, pugi::encoding_auto))
		{
			throw std::runtime_error("Failed to load XML file");
		}

		std::vector<pugi::xml_node> itemGroups;

		for (pugi::xml_node itemGroup = doc.child("Project").child("ItemGroup"); itemGroup; itemGroup = itemGroup.next_sibling("ItemGroup"))
		{
			itemGroups.push_back(itemGroup);
		}
		// 두 번째 ItemGroup (인덱스 1)에 헤더 파일 추가 (ClInclude)
		pugi::xml_node headerGroup = itemGroups[1];
		pugi::xml_node newHeader = headerGroup.append_child("ClInclude");
		newHeader.append_attribute("Include") = "Assets\\BehaviorTree\\ConditionDecorator\\" + conditionHeaderFileName;
		// 세 번째 ItemGroup (인덱스 2)에 소스 파일 추가 (ClCompile)
		pugi::xml_node cppGroup = itemGroups[2];
		pugi::xml_node newSource = cppGroup.append_child("ClCompile");
		newSource.append_attribute("Include") = "Assets\\BehaviorTree\\ConditionDecorator\\" + conditionBodyFileName;
		pugi::xml_node additionalOptionsDebug = newSource.append_child("AdditionalOptions");
		additionalOptionsDebug.append_attribute("Condition") = "'$(Configuration)|$(Platform)'=='Debug|x64'";
		additionalOptionsDebug.append_child(pugi::node_pcdata).set_value("/utf-8 %(AdditionalOptions)");
		pugi::xml_node additionalOptionsRelease = newSource.append_child("AdditionalOptions");
		additionalOptionsRelease.append_attribute("Condition") = "'$(Configuration)|$(Platform)'=='Release|x64'";
		additionalOptionsRelease.append_child(pugi::node_pcdata).set_value("/utf-8 %(AdditionalOptions)");

		if (!doc.save_file(conditionProjPath.c_str(), PUGIXML_TEXT("\t"), 1U, pugi::encoding_auto))
		{
			throw std::runtime_error("Failed to save XML file");
		}
	}
#endif // BUILD_FLAG
}

void HotLoadSystem::CreateAniBehaviorScript(std::string_view name)
{
#ifndef BUILD_FLAG
	if (!file::exists(PathFinder::Relative("Script")))
	{
		file::create_directories(PathFinder::Relative("Script"));
	}
	std::string scriptHeaderFileName = std::string(name) + ".h";
	std::string scriptBodyFileName = std::string(name) + ".cpp";
	std::string scriptHeaderFilePath = PathFinder::Relative("Script\\" + scriptHeaderFileName).string();
	std::string scriptBodyFilePath = PathFinder::Relative("Script\\" + scriptBodyFileName).string();
	std::string aniScriptFactoryPath = PathFinder::DynamicSolutionPath("AniBehaviorFactory.h").string();
	std::string aniScriptFactoryFuncPath = PathFinder::DynamicSolutionPath("funcMain.h").string();
	std::string scriptProjPath = PathFinder::DynamicSolutionPath("Dynamic_CPP.vcxproj").string();
	std::string scriptFilterPath = PathFinder::DynamicSolutionPath("Dynamic_CPP.vcxproj.filters").string();
	std::ofstream scriptFile(scriptHeaderFilePath);
	if (scriptFile.is_open())
	{
		scriptFile << AniBehaviorIncludeString 
			<< name << AniBehaviorInheritString 
			<< name << AniBehaviorEndString;
		scriptFile.close();
	}
	else
	{
		throw std::runtime_error("Failed to create ani behaviour file");
	}

	std::ofstream scriptBodyFile(scriptBodyFilePath);
	if (scriptBodyFile.is_open())
	{
		scriptBodyFile
			<< AniBehaviorCPPString
			<< name << AniBehaviorCPPEndString
			<< "void " << name << AniBehaviorCPPEndEnterBodyString
			<< "void " << name << AniBehaviorCPPEndUpdateString
			<< "void " << name << AniBehaviorCPPEndExitBodyString;
		scriptBodyFile.close();
	}
	else
	{
		throw std::runtime_error("Failed to create ani behaviour body file");
	}

	//Factory Add
	std::ifstream aniScriptFactoryFile(aniScriptFactoryPath);
	if (aniScriptFactoryFile.is_open())
	{
		std::stringstream buffer;
		buffer << aniScriptFactoryFile.rdbuf();
		std::string content = buffer.str();
		aniScriptFactoryFile.close();
		size_t posHeader = content.find(AniBehaviorMarkerFactoryHeaderString);
		if (posHeader != std::string::npos)
		{
			size_t endLine = content.find('\n', posHeader);
			if (endLine != std::string::npos)
			{
				content.insert(endLine + 1, AniBehaviorFactoryIncludeString.data() + scriptHeaderFileName + "\"\n");
			}
		}
		else
		{
			throw std::runtime_error("Failed to find marker in ani behaviour factory file");
		}
		std::ofstream aniScriptFactoryFileOut(aniScriptFactoryPath);
		if (aniScriptFactoryFileOut.is_open())
		{
			aniScriptFactoryFileOut << content;
			aniScriptFactoryFileOut.close();
		}
		else
		{
			throw std::runtime_error("Failed to create ani behaviour factory file");
		}
	}
	else
	{
		throw std::runtime_error("Failed to create ani behaviour factory file");
	}

	//Factory Func Add
	std::ifstream aniScriptFactoryFuncFile(aniScriptFactoryFuncPath);
	if (aniScriptFactoryFuncFile.is_open())
	{
		std::stringstream buffer;
		buffer << aniScriptFactoryFuncFile.rdbuf();
		std::string content = buffer.str();
		aniScriptFactoryFuncFile.close();
		size_t posFunc = content.find(AniBehaviorMarkerFactoryFuncString);
		if (posFunc != std::string::npos)
		{
			size_t endLine = content.find('\n', posFunc);
			if (endLine != std::string::npos)
			{
				content.insert(endLine + 1, AniBehaviorFactoryFunctionString.data() + std::string(name) + AniBehaviorFactoryFunctionLambdaString.data() + std::string(name) + AniBehaviorFactoryFunctionEndString.data());
			}
		}
		else
		{
			throw std::runtime_error("Failed to find marker in ani behaviour factory file");
		}
		std::ofstream aniScriptFactoryFuncFileOut(aniScriptFactoryFuncPath);
		if (aniScriptFactoryFuncFileOut.is_open())
		{
			aniScriptFactoryFuncFileOut << content;
			aniScriptFactoryFuncFileOut.close();
		}
		else
		{
			throw std::runtime_error("Failed to create ani behaviour factory file");
		}
	}
	else
	{
		throw std::runtime_error("Failed to create ani behaviour factory file");
	}

	{
		//Filter Add
		pugi::xml_document doc;
		if (!doc.load_file(scriptFilterPath.c_str(), pugi::parse_full, pugi::encoding_auto))
		{
			throw std::runtime_error("Failed to load XML file");
		}
		std::vector<pugi::xml_node> itemGroups;
		for (pugi::xml_node itemGroup = doc.child("Project").child("ItemGroup"); itemGroup; itemGroup = itemGroup.next_sibling("ItemGroup"))
		{
			itemGroups.push_back(itemGroup);
		}
		// 두 번째 ItemGroup (인덱스 1)에 헤더 파일 추가 (ClInclude)
		pugi::xml_node headerGroup = itemGroups[1];
		pugi::xml_node newHeader = headerGroup.append_child("ClInclude");
		newHeader.append_attribute("Include") = "Assets\\Script\\" + scriptHeaderFileName;
		pugi::xml_node filterNodeHeader = newHeader.append_child("Filter");
		filterNodeHeader.text().set("Script");
		// 세 번째 ItemGroup (인덱스 2)에 소스 파일 추가 (ClCompile)
		pugi::xml_node cppGroup = itemGroups[2];
		pugi::xml_node newSource = cppGroup.append_child("ClCompile");
		newSource.append_attribute("Include") = "Assets\\Script\\" + scriptBodyFileName;
		pugi::xml_node filterNodeSource = newSource.append_child("Filter");
		filterNodeSource.text().set("Script");
		
		if (!doc.save_file(scriptFilterPath.c_str(), PUGIXML_TEXT("\t"), 1U, pugi::encoding_auto))
		{
			throw std::runtime_error("Failed to save XML file");
		}
	}
	
	{
		//Proj Add
		pugi::xml_document doc;
		if (!doc.load_file(scriptProjPath.c_str(), pugi::parse_full, pugi::encoding_auto))
		{
			throw std::runtime_error("Failed to load XML file");
		}

		std::vector<pugi::xml_node> itemGroups;
		for (pugi::xml_node itemGroup = doc.child("Project").child("ItemGroup"); itemGroup; itemGroup = itemGroup.next_sibling("ItemGroup"))
		{
			itemGroups.push_back(itemGroup);
		}

		// 두 번째 ItemGroup (인덱스 1)에 헤더 파일 추가 (ClInclude)
		pugi::xml_node headerGroup = itemGroups[1];
		pugi::xml_node newHeader = headerGroup.append_child("ClInclude");
		newHeader.append_attribute("Include") = "Assets\\Script\\" + scriptHeaderFileName;

		// 세 번째 ItemGroup (인덱스 2)에 소스 파일 추가 (ClCompile)
		pugi::xml_node cppGroup = itemGroups[2];
		pugi::xml_node newSource = cppGroup.append_child("ClCompile");
		newSource.append_attribute("Include") = "Assets\\Script\\" + scriptBodyFileName;

		pugi::xml_node additionalOptionsDebug = newSource.append_child("AdditionalOptions");
		additionalOptionsDebug.append_attribute("Condition") = "'$(Configuration)|$(Platform)'=='Debug|x64'";
		additionalOptionsDebug.append_child(pugi::node_pcdata).set_value("/utf-8 %(AdditionalOptions)");

		pugi::xml_node additionalOptionsRelease = newSource.append_child("AdditionalOptions");
		additionalOptionsRelease.append_attribute("Condition") = "'$(Configuration)|$(Platform)'=='Release|x64'";
		additionalOptionsRelease.append_child(pugi::node_pcdata).set_value("/utf-8 %(AdditionalOptions)");

		if (!doc.save_file(scriptProjPath.c_str(), PUGIXML_TEXT("\t"), 1U, pugi::encoding_auto))
		{
			throw std::runtime_error("Failed to save XML file");
		}
	}
#endif
}

void HotLoadSystem::ResetAniBehaviorPtr()
{
	std::unique_lock lock(m_scriptFileMutex);
	for (auto& aniBehavior : m_aniBehaviorStates)
	{
		if (aniBehavior)
		{
			aniBehavior->ResetBehaviour();
		}
	}
}

void HotLoadSystem::Compile()
{
#ifndef BUILD_FLAG
	file::path scriptPath = PathFinder::Relative("Script\\");

	for(auto& iterPath : file::recursive_directory_iterator(scriptPath))
	{
		if (iterPath.is_regular_file() && iterPath.path().extension() == ".cpp")
		{
			std::string scriptName = iterPath.path().stem().string();
			DataSystems->GetAssetMetaWatcher()->CreateYamlMeta(iterPath);
		}
	}
#endif

	if (hDll)
	{
#ifndef BUILD_FLAG
		AIManagers->ClearTreeInAIComponent();

		for (auto& [gameObject, index, name] : m_scriptComponentIndexs)
		{
			auto script = std::dynamic_pointer_cast<ModuleBehavior>(gameObject->m_components[index]);
			if (nullptr != script)
			{
				script->OnDestroy();
				// 스크립트 재 컴파일 할 경우 복원할 이전 값 직렬화
				void* scriptPtr = reinterpret_cast<void*>(gameObject->m_components[index].get());
				auto& type = script->ScriptReflect();
				if(!type.name.empty())
				{
					auto scriptNode = Meta::Serialize(scriptPtr, type);
					m_scriptComponentMetaIndexs.emplace_back(gameObject, index, scriptNode);
				}

				UnbindScriptEvents(script.get(), name);
				gameObject->m_components[index].reset();
			}
		}

		for(auto& scriptName : m_scriptNames)
		{
			UnRegisterScriptReflection(scriptName);
		}

		Meta::UndoCommandManager->Clear();
		Meta::UndoCommandManager->ClearGameMode();

		ResetAniBehaviorPtr();

		while(GetModuleLoadCount(hDll) > 0)
		{
			FreeLibrary(hDll);
		}
		m_scriptFactoryFunc		= nullptr;
		m_scriptNamesFunc		= nullptr;
		//m_setSceneManagerFunc	= nullptr;
		hDll					= nullptr;
#endif
	}

#ifndef BUILD_FLAG
	if (EngineSettingInstance->GetMSVCVersion() != MSVCVersion::None)
	{
		bool buildSuccess = false;
		for (int attempt = 1; attempt <= MAX__COMPILE_ITERATION; ++attempt)
		{
			try
			{
				RunMsbuildWithLiveLog(command);
				buildSuccess = true;
				break; // 성공 시 반복 종료
			}
			catch (const std::exception& e)
			{
				g_progressWindow->SetStatusText(L"Build failed...(" + std::to_wstring(attempt) + L"/" + std::to_wstring(MAX__COMPILE_ITERATION) + L")");
				if (attempt == MAX__COMPILE_ITERATION)
				{
					m_isReloading = false;
					g_progressWindow->SetStatusText(L"Build failed...");
					throw std::runtime_error("Build failed after 4 attempts");
				}
				else
				{
					std::this_thread::sleep_for(std::chrono::seconds(1));
				}
			}
		}
	}
	else
	{
		Debug->LogError("MSBuild path is not set. Please check your Visual Studio installation.");
	}
#endif

	file::path dllPath = PathFinder::RelativeToExecutable("Dynamic_CPP.dll");
	hDll = LoadLibraryA(dllPath.string().c_str());
	if (!hDll)
	{
		m_isReloading = false;
		g_progressWindow->SetStatusText(L"Failed to load library...");
		throw std::runtime_error("Failed to load library");
	}
	// 스크립트 팩토리 함수 가져오기
	m_scriptFactoryFunc = reinterpret_cast<ModuleBehaviorFunc>(GetProcAddress(hDll, "CreateModuleBehavior"));
	if (!m_scriptFactoryFunc)

	{
		m_isReloading = false;
		g_progressWindow->SetStatusText(L"Failed to get function address...");
		throw std::runtime_error("Failed to get function address");
	}

	// 스크립트 힙영역 할당 해제 함수 가져오기
	m_scriptDeleteFunc = reinterpret_cast<ModuleBehaviorDeleteFunc>(GetProcAddress(hDll, "DeleteModuleBehavior"));
	if (!m_scriptDeleteFunc)
	{
		m_isReloading = false;
		g_progressWindow->SetStatusText(L"Failed to get function address...");
		throw std::runtime_error("Failed to get function address");
	}

	// 스크립트 이름 함수 가져오기
	m_scriptNamesFunc = reinterpret_cast<GetScriptNamesFunc>(GetProcAddress(hDll, "ListModuleBehavior"));
	if (!m_scriptNamesFunc)
	{
		m_isReloading = false;
		g_progressWindow->SetStatusText(L"Failed to get function address...");
		throw std::runtime_error("Failed to get function address");
	}
	// 행동 트리 노드 함수 가져오기
	m_btActionNodeFunc = reinterpret_cast<BTActionNodeFunc>(GetProcAddress(hDll, "CreateBTActionNode"));
	if (!m_btActionNodeFunc)
	{
		m_isReloading = false;
		g_progressWindow->SetStatusText(L"Failed to get function address...");
		throw std::runtime_error("Failed to get function address...");
	}
	// 행동 트리 할당 채제 함수 가져오기
	m_btActionNodeDeleteFunc = reinterpret_cast<BTActionNodeDeleteFunc>(GetProcAddress(hDll, "DeleteBTActionNode"));
	if (!m_btActionNodeDeleteFunc)
	{
		m_isReloading = false;
		g_progressWindow->SetStatusText(L"Failed to get function address...");
		throw std::runtime_error("Failed to get function address...");
	}

	// 행동 트리 조건 노드 함수 가져오기
	m_btConditionNodeFunc = reinterpret_cast<BTConditionNodeFunc>(GetProcAddress(hDll, "CreateBTConditionNode"));
	if (!m_btConditionNodeFunc)
	{
		m_isReloading = false;
		g_progressWindow->SetStatusText(L"Failed to get function address...");
		throw std::runtime_error("Failed to get function address...");
	}
	// 행동 트리 조건 노드 할당 해제 함수 가져오기
	m_btConditionNodeDeleteFunc = reinterpret_cast<BTConditionNodeDeleteFunc>(GetProcAddress(hDll, "DeleteBTConditionNode"));
	if (!m_btConditionNodeDeleteFunc)
	{
		m_isReloading = false;
		g_progressWindow->SetStatusText(L"Failed to get function address...");
		throw std::runtime_error("Failed to get function address...");
	}

	// 행동 트리 스크립트 데코레이터 할당 함수 가져오기
	m_btConditionDecoratorNodeFunc = reinterpret_cast<BTConditionDecoratorNodeFunc>(GetProcAddress(hDll, "CreateBTConditionDecoratorNode"));
	if (!m_btConditionDecoratorNodeFunc)
	{
		m_isReloading = false;
		g_progressWindow->SetStatusText(L"Failed to get function address...");
		throw std::runtime_error("Failed to get function address...");
	}
	// 행동 트리 스크립트 데코레이터 할당 해제 함수 가져오기
	m_btConditionDecoratorNodeDeleteFunc = reinterpret_cast<BTConditionDecoratorNodeDeleteFunc>(GetProcAddress(hDll, "DeleteBTConditionDecoratorNode"));
	if (!m_btConditionDecoratorNodeDeleteFunc)
	{
		m_isReloading = false;
		g_progressWindow->SetStatusText(L"Failed to get funcion address...");
		throw std::runtime_error("Failed to get funcion address...");
	}

	// 행동 트리 액션 노드 이름 함수 가져오기
	m_listBTActionNodeNamesFunc = reinterpret_cast<ListBTActionNodeNamesFunc>(GetProcAddress(hDll, "ListBTActionNode"));
	if (!m_listBTActionNodeNamesFunc)
	{
		m_isReloading = false;
		g_progressWindow->SetStatusText(L"Failed to get function address...");
		throw std::runtime_error("Failed to get function address...");
	}
	// 행동 트리 조건 노드 이름 함수 가져오기
	m_listBTConditionNodeNamesFunc = reinterpret_cast<ListBTConditionNodeNamesFunc>(GetProcAddress(hDll, "ListBTConditionNode"));
	if (!m_listBTConditionNodeNamesFunc)
	{
		m_isReloading = false;
		g_progressWindow->SetStatusText(L"Failed to get function address...");
		throw std::runtime_error("Failed to get function address...");
	}
	// 행동 트리 데코레이터 노드 이름 함수 가져오기
	m_listBTConditionDecoratorNodeNamesFunc = reinterpret_cast<ListBTConditionDecoratorNodeNamesFunc>(GetProcAddress(hDll, "ListBTConditionDecoratorNode"));
	if (!m_listBTConditionDecoratorNodeNamesFunc)
	{
		m_isReloading = false;
		g_progressWindow->SetStatusText(L"Failed to get function address...");
		throw std::runtime_error("Failed to get function address...");
	}
	// 에니메이션 행동 스크립트 이름 함수 가져오기
	m_listAniBehaviorNamesFunc = reinterpret_cast<ListAniBehaviorNamesFunc>(GetProcAddress(hDll, "ListAniBehavior"));
	if (!m_listAniBehaviorNamesFunc)
	{
		m_isReloading = false;
		g_progressWindow->SetStatusText(L"Failed to get function address...");
		throw std::runtime_error("Failed to get function address...");
	}

	// 에니메이션 행동 스크립트 함수 가져오기
	m_AniBehaviorFunc = reinterpret_cast<AniBehaviorFunc>(GetProcAddress(hDll, "CreateAniBehavior"));
	if (!m_AniBehaviorFunc)
	{
		m_isReloading = false;
		g_progressWindow->SetStatusText(L"Failed to get function address...");
		throw std::runtime_error("Failed to get function address...");
	}

	// 에니메이션 행동 스크립트 할당 해제 함수 가져오기
	m_AniBehaviorDeleteFunc = reinterpret_cast<AniBehaviorDeleteFunc>(GetProcAddress(hDll, "DeleteAniBehavior"));
	if (!m_AniBehaviorDeleteFunc)
	{
		m_isReloading = false;
		g_progressWindow->SetStatusText(L"Failed to get function address...");
		throw std::runtime_error("Failed to get function address...");
	}

	m_isCompileEventInvoked = false;
	m_isReloading = true;
}
#endif // !DYNAMICCPP_EXPORTS