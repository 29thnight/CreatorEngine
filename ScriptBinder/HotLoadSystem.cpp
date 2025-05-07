#include "HotLoadSystem.h"
#include "LogSystem.h"
#include "GameObject.h"
#include "SceneManager.h"
#include "ModuleBehavior.h"
#include "pugixml.hpp"
#include "ReflectionYml.h"

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
    si.hStdError = hWrite; // 오류도 같은 파이프로
    si.hStdInput = NULL;

    PROCESS_INFORMATION pi;

    std::wstring fullCommand = commandLine;

    if (!CreateProcessW(NULL, &fullCommand[0], NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi))
    {
        throw std::runtime_error("Build failed");
    }

    CloseHandle(hWrite); // 부모는 읽기만 하면 됨

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

            //std::string utf8Line = AnsiToUtf8(line);
			Debug->LogDebug(line);
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
    
	command = std::wstring(L"cmd /c \"")
        + L"\"" + msbuildPath + L"\" "
        + L"\"" + slnPath + L"\" "
        + L"/m /t:Build /p:Configuration=Debug /p:Platform=x64 /nologo"
        + L"\"";

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

void HotLoadSystem::TrackScriptChanges()
{
}

void HotLoadSystem::ReloadDynamicLibrary()
{
	if(true == m_isCompileEventInvoked)
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
	}
}

void HotLoadSystem::ReplaceScriptComponent()
{
	if (true == m_isReloading && false == m_isCompileEventInvoked)
	{
		for (auto& [gameObject, index, name] : m_scriptComponentIndexs)
		{
			auto* newScript = CreateMonoBehavior(name.c_str());
			ScriptManager->BindScriptEvents(newScript, name);
			newScript->SetOwner(gameObject);
			gameObject->m_components[index].reset(newScript);
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
	std::string scriptBodyFileName = std::string(name) + ".cpp";
	FileGuid guid = DataSystems->GetFilenameToGuid(scriptBodyFileName);
	file::path scriptFullPath = DataSystems->GetFilePath(guid);
	file::path scriptMetaPath = scriptFullPath += L".meta";

	if (file::exists(scriptMetaPath))
	{
		MetaYml::Node scriptNode = MetaYml::LoadFile(scriptMetaPath.string());
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
					SceneManagers->GetActiveScene()->StartEvent.AddLambda([=]() 
					{
						if (false == script->m_isCallStart)
						{
							script->Start();
							script->m_isCallStart = true;
						}
					});
				}
				else if (event == "FixedUpdate")
				{
					SceneManagers->GetActiveScene()->FixedUpdateEvent.AddRaw(script, &ModuleBehavior::FixedUpdate);
				}
				else if (event == "OnTriggerEnter")
				{
					SceneManagers->GetActiveScene()->OnTriggerEnterEvent.AddRaw(script, &ModuleBehavior::OnTriggerEnter);
				}
				else if (event == "OnTriggerStay")
				{
					SceneManagers->GetActiveScene()->OnTriggerStayEvent.AddRaw(script, &ModuleBehavior::OnTriggerStay);
				}
				else if (event == "OnTriggerExit")
				{
					SceneManagers->GetActiveScene()->OnTriggerExitEvent.AddRaw(script, &ModuleBehavior::OnTriggerExit);
				}
				else if (event == "OnCollisionEnter")
				{
					SceneManagers->GetActiveScene()->OnCollisionEnterEvent.AddRaw(script, &ModuleBehavior::OnCollisionEnter);
				}
				else if (event == "OnCollisionStay")
				{
					SceneManagers->GetActiveScene()->OnCollisionStayEvent.AddRaw(script, &ModuleBehavior::OnCollisionStay);
				}
				else if (event == "OnCollisionExit")
				{
					SceneManagers->GetActiveScene()->OnCollisionExitEvent.AddRaw(script, &ModuleBehavior::OnCollisionExit);
				}
				else if (event == "Update")
				{
					SceneManagers->GetActiveScene()->UpdateEvent.AddRaw(script, &ModuleBehavior::Update);
				}
				else if (event == "LateUpdate")
				{
					SceneManagers->GetActiveScene()->LateUpdateEvent.AddRaw(script, &ModuleBehavior::LateUpdate);
				}
			}
			
		}

	}

}

void HotLoadSystem::UnbindScriptEvents(ModuleBehavior* script, const std::string_view& name)
{

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
			<< scriptCppEndFixedUpdateString
			<< name
			<< scriptCppEndOnTriggerEnterString
			<< name
			<< scriptCppEndOnTriggerStayString
			<< name
			<< scriptCppEndOnTriggerExitString
			<< name
			<< scriptCppEndOnCollisionEnterString
			<< name
			<< scriptCppEndOnCollisionStayString
			<< name
			<< scriptCppEndOnCollisionExitString
			<< name
			<< scriptCppEndUpdateString
			<< name
			<< scriptCppEndLateUpdateString;
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
		newSource.append_attribute("Include") = "\\Assets\\Script\\" + scriptBodyFileName;
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
		newHeader.append_attribute("Include") = "\\Assets\\Script\\" + scriptHeaderFileName;

		// 세 번째 ItemGroup (인덱스 2)에 소스 파일 추가 (ClCompile)
		pugi::xml_node cppGroup = itemGroups[2];
		pugi::xml_node newSource = cppGroup.append_child("ClCompile");
		newSource.append_attribute("Include") = "\\Assets\\Script\\" + scriptBodyFileName;

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
		FreeLibrary(hDll);
		hDll = nullptr;
	}

    try
    {
        RunMsbuildWithLiveLog(command);
    }
    catch (const std::exception& e)
    {
		m_isReloading = false;
        throw std::runtime_error("Build failed");
    }

	hDll = LoadLibraryA(PathFinder::RelativeToExecutable("Dynamic_CPP.dll").string().c_str());
	if (!hDll)
	{
		m_isReloading = false;
		throw std::runtime_error("Failed to load library");
	}

	m_scriptFactoryFunc = reinterpret_cast<ModuleBehaviorFunc>(GetProcAddress(hDll, "CreateModuleBehavior"));
	if (!m_scriptFactoryFunc)
	{
		m_isReloading = false;
		throw std::runtime_error("Failed to get function address");
	}

	m_initModuleFunc = reinterpret_cast<InitModuleFunc>(GetProcAddress(hDll, "InitModuleFactory"));
	if (!m_initModuleFunc)
	{
		m_isReloading = false;
		throw std::runtime_error("Failed to get function address");
	}

	m_scriptNamesFunc = reinterpret_cast<GetScriptNamesFunc>(GetProcAddress(hDll, "ListModuleBehavior"));
	if (!m_scriptNamesFunc)
	{
		m_isReloading = false;
		throw std::runtime_error("Failed to get function address");
	}

	m_setSceneManagerFunc = reinterpret_cast<SetSceneManagerFunc>(GetProcAddress(hDll, "SetSceneManager"));
	if (!m_setSceneManagerFunc)
	{
		m_isReloading = false;
		throw std::runtime_error("Failed to get function address");
	}
	m_isCompileEventInvoked = false;
	m_isReloading = true;
}
