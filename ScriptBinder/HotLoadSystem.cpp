#include "HotLoadSystem.h"
#include "LogSystem.h"
#include "GameObject.h"
#include "SceneManager.h"
#include "ModuleBehavior.h"
#include "pugixml.hpp"
#include "ReflectionYml.h"
#include "ProgressWindow.h"

#pragma comment(linker,"\"/manifestdependency:type='win32' \
name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

#pragma region Script Event Binding Helper
#define START_EVENT_BIND_HELPER(ScriptPtr) \
	ScriptPtr->m_startEventHandle = SceneManagers->GetActiveScene()->StartEvent.AddLambda([=]() \
	{ \
		if (false == ScriptPtr->m_isCallStart) \
		{ \
			ScriptPtr->Start(); \
			ScriptPtr->m_isCallStart = true; \
		} \
	})

#define FIXED_UPDATE_EVENT_BIND_HELPER(ScriptPtr) \
	ScriptPtr->m_fixedUpdateEventHandle = SceneManagers->GetActiveScene()->FixedUpdateEvent.AddRaw(ScriptPtr, &ModuleBehavior::FixedUpdate)

#define UPDATE_EVENT_BIND_HELPER(ScriptPtr) \
	ScriptPtr->m_updateEventHandle = SceneManagers->GetActiveScene()->UpdateEvent.AddRaw(ScriptPtr, &ModuleBehavior::Update)

#define LATE_UPDATE_EVENT_BIND_HELPER(ScriptPtr) \
	ScriptPtr->m_lateUpdateEventHandle = SceneManagers->GetActiveScene()->LateUpdateEvent.AddRaw(ScriptPtr, &ModuleBehavior::LateUpdate)

#define ON_ENABLE_EVENT_BIND_HELPER(ScriptPtr) \
	ScriptPtr->m_onEnableEventHandle = SceneManagers->GetActiveScene()->OnEnableEvent.AddRaw(ScriptPtr, &ModuleBehavior::OnEnable)

#define ON_DISABLE_EVENT_BIND_HELPER(ScriptPtr) \
	ScriptPtr->m_onDisableEventHandle = SceneManagers->GetActiveScene()->OnDisableEvent.AddRaw(ScriptPtr, &ModuleBehavior::OnDisable)

#pragma endregion

std::string AnsiToUtf8(const std::string& ansiStr)
{
    // ANSI → Wide
    int wideLen = MultiByteToWideChar(CP_ACP, 0, ansiStr.c_str(), -1, nullptr, 0);
    std::wstring wide(wideLen, 0);
    MultiByteToWideChar(CP_ACP, 0, ansiStr.c_str(), -1, &wide[0], wideLen);

    // Wide → UTF-8
    int utf8Len = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string utf8(utf8Len, 0);
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, &utf8[0], utf8Len, nullptr, nullptr);

    return utf8;
}

void RunMsbuildWithLiveLog(const std::wstring& commandLine)
{
    HANDLE hRead, hWrite;
    SECURITY_ATTRIBUTES sa = { sizeof(SECURITY_ATTRIBUTES), NULL, TRUE };
    if (!CreatePipe(&hRead, &hWrite, &sa, 0))
    {
        __debugbreak();
    }

    STARTUPINFOW si = { sizeof(STARTUPINFOW) };
    si.dwFlags |= STARTF_USESTDHANDLES;
    si.hStdOutput = hWrite;
    si.hStdError = hWrite;
    si.hStdInput = NULL;

    PROCESS_INFORMATION pi;

    std::wstring fullCommand = commandLine;

    if (!CreateProcessW(NULL, &fullCommand[0], NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi))
    {
        throw std::runtime_error("Build failed");
    }

    CloseHandle(hWrite);

    char buffer[4096]{};
    DWORD bytesRead;
    std::string leftover;

    while (ReadFile(hRead, buffer, sizeof(buffer) - 1, &bytesRead, nullptr))
    {
        if (bytesRead == 0) break;
        buffer[bytesRead] = '\0';

        leftover += buffer;

        size_t pos;
        while ((pos = leftover.find('\n')) != std::string::npos)
        {
            std::string line = leftover.substr(0, pos);
            leftover.erase(0, pos + 1);

            if (line.empty())
            {
                continue;
            }

			if (line.find("error") != std::string::npos || line.find("error C") != std::string::npos)
			{
				Debug->LogError(line);
			}
			else if (line.find("오류") != std::string::npos || line.find("경고") != std::string::npos)
			{
				Debug->LogDebug(line);
			}
			else
			{
				std::string strLine = AnsiToUtf8(line);
				Debug->LogDebug(strLine);
			}
		}
    }

    WaitForSingleObject(pi.hProcess, INFINITE);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(hRead);
}

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
        + L"/m /t:Clean;Build /p:Configuration=Debug /p:Platform=x64 /nologo"
        + L"\"";
#else
	command = std::wstring(L"cmd /c \"")
		+ L"\"" + msbuildPath + L"\" "
		+ L"\"" + slnPath + L"\" "
		+ L"/m /t:Clean;Build /p:Configuration=Release /p:Platform=x64 /nologo"
		+ L"\"";
#endif

	try
	{
		Compile();
	}
	catch (const std::exception& e)
	{
		Debug->LogError("Failed to compile script: " + std::string(e.what()));
		return;
	}

	m_initModuleFunc();
	const char** scriptNames = nullptr;
	int scriptCount = 0;
	scriptNames = m_scriptNamesFunc(&scriptCount);

	for (int i = 0; i < scriptCount; ++i)
	{
		std::string scriptName = scriptNames[i];
		std::string scriptFileName = std::string(scriptName);
		m_scriptNames.push_back(scriptName);
	}

}

void HotLoadSystem::Shutdown()
{
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
		return false; // 처음 시작할 때는 항상 빌드 필요
	}

	file::path dllPath = PathFinder::RelativeToExecutable("Dynamic_CPP.dll");
	file::path slnPath = PathFinder::DynamicSolutionPath("Dynamic_CPP.sln");

	if (!file::exists(dllPath))
		return false;

	auto dllTimeStamp = file::last_write_time(dllPath);

	for (const auto& scriptName : m_scriptNames)
	{
		file::path scriptPath = PathFinder::Relative("Script\\" + scriptName + ".cpp");

		if (file::exists(scriptPath))
		{
			auto scriptTimeStamp = file::last_write_time(scriptPath);

			if (scriptTimeStamp > dllTimeStamp)
			{
				return false; // 빌드 필요함 (스크립트가 더 최신임)
			}
		}
	}

	return true; // 모든 스크립트가 DLL보다 오래됨 (업투데이트됨)
}

void HotLoadSystem::ReloadDynamicLibrary()
{
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
			return;
		}

		m_initModuleFunc();
		m_scriptNames.clear();
		const char** scriptNames = nullptr;
		int scriptCount = 0;
		scriptNames = m_scriptNamesFunc(&scriptCount);

		for (int i = 0; i < scriptCount; ++i)
		{
			std::string scriptName = scriptNames[i];
			std::string scriptFileName = std::string(scriptName);
			m_scriptNames.push_back(scriptName);
		}

		ReplaceScriptComponent();
		g_progressWindow->Close();
	}
}

void HotLoadSystem::ReplaceScriptComponent()
{
	if (true == m_isReloading && false == m_isCompileEventInvoked)
	{
		for (auto& [gameObject, index, name] : m_scriptComponentIndexs)
		{
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
			//gameObject->m_components[index].reset();
			gameObject->m_components[index].swap(sharedScript);
			newScript->m_scriptGuid = DataSystems->GetFilenameToGuid(name + ".cpp");
		}
		m_isReloading = false;
	}
}

void HotLoadSystem::CompileEvent()
{
	m_isCompileEventInvoked = true;
}

void HotLoadSystem::BindScriptEvents(ModuleBehavior* script, const std::string_view& name)
{
	//결국 이렇게되면 .meta파일을 클라이언트가 가지고 있어야 됨...
	std::string scriptMetaFile = "Assets\\Script\\" + std::string(name) + ".cpp" + ".meta";
	file::path scriptMetaFileName = PathFinder::DynamicSolutionPath(scriptMetaFile);

	if (file::exists(scriptMetaFileName))
	{
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
				if (event == "Start")
				{
					START_EVENT_BIND_HELPER(script);
				}
				else if (event == "FixedUpdate")
				{
					FIXED_UPDATE_EVENT_BIND_HELPER(script);
				}
				//TODO : Physics System 완료되면 추가할 것
				//else if (event == "OnTriggerEnter")
				//{
				//	//SceneManagers->GetActiveScene()->OnTriggerEnterEvent.AddRaw(script, &ModuleBehavior::OnTriggerEnter);
				//}
				//else if (event == "OnTriggerStay")
				//{
				//	//SceneManagers->GetActiveScene()->OnTriggerStayEvent.AddRaw(script, &ModuleBehavior::OnTriggerStay);
				//}
				//else if (event == "OnTriggerExit")
				//{
				//	//SceneManagers->GetActiveScene()->OnTriggerExitEvent.AddRaw(script, &ModuleBehavior::OnTriggerExit);
				//}
				//else if (event == "OnCollisionEnter")
				//{
				//	//SceneManagers->GetActiveScene()->OnCollisionEnterEvent.AddRaw(script, &ModuleBehavior::OnCollisionEnter);
				//}
				//else if (event == "OnCollisionStay")
				//{
				//	//SceneManagers->GetActiveScene()->OnCollisionStayEvent.AddRaw(script, &ModuleBehavior::OnCollisionStay);
				//}
				//else if (event == "OnCollisionExit")
				//{
				//	//SceneManagers->GetActiveScene()->OnCollisionExitEvent.AddRaw(script, &ModuleBehavior::OnCollisionExit);
				//}
				else if (event == "Update")
				{
					UPDATE_EVENT_BIND_HELPER(script);
				}
				else if (event == "LateUpdate")
				{
					LATE_UPDATE_EVENT_BIND_HELPER(script);
				}
			}
			
		}
	}
}

void HotLoadSystem::UnbindScriptEvents(ModuleBehavior* script, const std::string_view& name)
{
	if (0 != script->m_startEventHandle.GetID())
	{
		SceneManagers->GetActiveScene()->StartEvent.Remove(script->m_startEventHandle);
	}

	if (0 != script->m_fixedUpdateEventHandle.GetID())
	{
		SceneManagers->GetActiveScene()->FixedUpdateEvent.Remove(script->m_fixedUpdateEventHandle);
	}

	if (0 != script->m_updateEventHandle.GetID())
	{
		SceneManagers->GetActiveScene()->UpdateEvent.Remove(script->m_updateEventHandle);
	}

	if (0 != script->m_lateUpdateEventHandle.GetID())
	{
		SceneManagers->GetActiveScene()->LateUpdateEvent.Remove(script->m_lateUpdateEventHandle);
	}
}

void HotLoadSystem::CreateScriptFile(const std::string_view& name)
{
	std::string scriptHeaderFileName = std::string(name) + ".h";
	std::string scriptBodyFileName = std::string(name) + ".cpp";
	std::string scriptHeaderFilePath = PathFinder::Relative("Script\\" + scriptHeaderFileName).string();
	std::string scriptBodyFilePath = PathFinder::Relative("Script\\" + scriptBodyFileName).string();
	std::string scriptFactoryPath = PathFinder::DynamicSolutionPath("CreateFactory.h").string();
	std::string scriptFactoryFuncPath = PathFinder::DynamicSolutionPath("dllmain.cpp").string();
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
				content.insert(endLine + 1, scriptFactoryIncludeString + scriptHeaderFileName + "\"\n");
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
				content.insert(endLine + 1, scriptFactoryFunctionString + name.data() + scriptFactoryFunctionLambdaString + name.data() + scriptFactoryFunctionEndString);
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
}

void HotLoadSystem::Compile()
{
	if (hDll)
	{
		for (auto& [gameObject, index, name] : m_scriptComponentIndexs)
		{
			auto* script = static_cast<ModuleBehavior*>(gameObject->m_components[index].get());
			if (script)
			{
				UnbindScriptEvents(script, name);
			}
			gameObject->m_components[index].reset();
		}
		FreeLibrary(hDll);
		hDll = nullptr;
	}

    try
    {
		if (!IsScriptUpToDate()) RunMsbuildWithLiveLog(command);
    }
    catch (const std::exception& e)
    {
		m_isReloading = false;
		g_progressWindow->SetStatusText(L"Build failed...");
        throw std::runtime_error("Build failed");
    }

	hDll = LoadLibraryA(PathFinder::RelativeToExecutable("Dynamic_CPP.dll").string().c_str());
	if (!hDll)
	{
		m_isReloading = false;
		g_progressWindow->SetStatusText(L"Failed to load library...");
		throw std::runtime_error("Failed to load library");
	}

	m_scriptFactoryFunc = reinterpret_cast<ModuleBehaviorFunc>(GetProcAddress(hDll, "CreateModuleBehavior"));
	if (!m_scriptFactoryFunc)

	{
		m_isReloading = false;
		g_progressWindow->SetStatusText(L"Failed to get function address...");
		throw std::runtime_error("Failed to get function address");
	}

	m_initModuleFunc = reinterpret_cast<InitModuleFunc>(GetProcAddress(hDll, "InitModuleFactory"));
	if (!m_initModuleFunc)
	{
		m_isReloading = false;
		g_progressWindow->SetStatusText(L"Failed to get function address...");
		throw std::runtime_error("Failed to get function address");
	}

	m_scriptNamesFunc = reinterpret_cast<GetScriptNamesFunc>(GetProcAddress(hDll, "ListModuleBehavior"));
	if (!m_scriptNamesFunc)
	{
		m_isReloading = false;
		g_progressWindow->SetStatusText(L"Failed to get function address...");
		throw std::runtime_error("Failed to get function address");
	}

	m_setSceneManagerFunc = reinterpret_cast<SetSceneManagerFunc>(GetProcAddress(hDll, "SetSceneManager"));
	if (!m_setSceneManagerFunc)
	{
		m_isReloading = false;
		g_progressWindow->SetStatusText(L"Failed to get function address...");
		throw std::runtime_error("Failed to get function address");
	}

	m_isCompileEventInvoked = false;
	m_isReloading = true;
}
