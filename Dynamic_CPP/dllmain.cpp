// dllmain.cpp : DLL 애플리케이션의 진입점을 정의합니다.
#include "pch.h"
#include "Export.h"
#include "CreateFactory.h"

BOOL APIENTRY DllMain( HMODULE hModule,
                       DWORD  ul_reason_for_call,
                       LPVOID lpReserved
                     )
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}

extern "C"
{
	EXPORT_API ModuleBehavior* CreateModuleBehavior(const char* className)
	{
		std::string classNameStr(className);
		return CreateFactory::GetInstance()->CreateInstance(classNameStr);
	}

	EXPORT_API std::vector<std::string> ListModuleBehavior()
	{
		std::vector<std::string> nameVector{};
		for (auto& [name, func] : ModuleFactory->factoryMap)
		{
			nameVector.push_back(name);
		}

		return nameVector;
	}

	EXPORT_API void InitModuleFactory()
	{
		// Register the factory function for TestBehavior Automation
		CreateFactory::GetInstance()->RegisterFactory("TestScriptClass", []() { return new TestScriptClass(); });
		CreateFactory::GetInstance()->RegisterFactory("TestBehavior", []() { return new TestBehavior(); });
	}
}

