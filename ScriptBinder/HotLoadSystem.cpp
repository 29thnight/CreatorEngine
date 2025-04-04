#include "HotLoadSystem.h"
#include "LogSystem.h"
#include "GameObject.h"
#include "ModuleBehavior.h"
#include "pugixml.hpp"

void HotLoadSystem::Initialize()
{
	command = std::string
	{
		std::string("cmd /c \"") +
		vcvarsall +
		" && msbuild " +
		PathFinder::DynamicSolutionPath("Dynamic_CPP.sln").string() +
		" /m /t:Build /p:Configuration=Debug /p:Platform=x64 /nologo\""
	};

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
	m_scriptNames = m_scriptNamesFunc();
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
		m_initModuleFunc();
		m_scriptNames = m_scriptNamesFunc();

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
			gameObject->m_components[index] = newScript;
		}
		m_isReloading = false;
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
			<< scriptBodyString 
			<< scriptToStringFunString 
			<< name 
			<< scriptToStringEndString 
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
		newHeader.append_attribute("Include") = "..\\x64\\Assets\\Script\\" + scriptHeaderFileName;
		pugi::xml_node filterNodeHeader = newHeader.append_child("Filter");
		filterNodeHeader.text().set("Script\\ScriptClass");

		// 세 번째 ItemGroup (인덱스 2)에 소스 파일 추가 (ClCompile)
		pugi::xml_node cppGroup = itemGroups[2];
		pugi::xml_node newSource = cppGroup.append_child("ClCompile");
		newSource.append_attribute("Include") = "..\\x64\\Assets\\Script\\" + scriptBodyFileName;
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
		newHeader.append_attribute("Include") = "..\\x64\\Assets\\Script\\" + scriptHeaderFileName;

		// 세 번째 ItemGroup (인덱스 2)에 소스 파일 추가 (ClCompile)
		pugi::xml_node cppGroup = itemGroups[2];
		pugi::xml_node newSource = cppGroup.append_child("ClCompile");
		newSource.append_attribute("Include") = "..\\x64\\Assets\\Script\\" + scriptBodyFileName;

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

	int result = system(command.c_str());
	if (result != 0)
	{
		throw std::runtime_error("Build failed");
	}

	hDll = LoadLibraryA(PathFinder::RelativeToExecutable("Dynamic_CPP.dll").string().c_str());
	if (!hDll)
	{
		throw std::runtime_error("Failed to load assembly");
	}

	m_scriptFactoryFunc = reinterpret_cast<ModuleBehaviorFunc>(GetProcAddress(hDll, "CreateModuleBehavior"));
	if (!m_scriptFactoryFunc)
	{
		throw std::runtime_error("Failed to get function address");
	}

	m_initModuleFunc = reinterpret_cast<InitModuleFunc>(GetProcAddress(hDll, "InitModuleFactory"));
	if (!m_initModuleFunc)
	{
		throw std::runtime_error("Failed to get function address");
	}

	m_scriptNamesFunc = reinterpret_cast<GetScriptNamesFunc>(GetProcAddress(hDll, "ListModuleBehavior"));
	if (!m_scriptNamesFunc)
	{
		throw std::runtime_error("Failed to get function address");
	}

	m_isCompileEventInvoked = false;
	m_isReloading = true;
}
