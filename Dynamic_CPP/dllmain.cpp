// dllmain.cpp : DLL 애플리케이션의 진입점을 정의합니다.
#include "pch.h"
#include "Export.h"
#include "CreateFactory.h"
#include "SceneManager.h"

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
#pragma region Exported Functions
	EXPORT_API ModuleBehavior* CreateModuleBehavior(const char* className)
	{
		std::string classNameStr(className);
		return CreateFactory::GetInstance()->CreateInstance(classNameStr);
	}

	EXPORT_API const char** ListModuleBehavior(int* outCount)
	{
		static std::vector<std::string> nameVector;
		static std::vector<const char*> cstrs;

		nameVector.clear();
		cstrs.clear();

		for (const auto& [name, func] : ModuleFactory->factoryMap)
		{
			nameVector.push_back(name);
		}

		for (auto& name : nameVector)
		{
			cstrs.push_back(name.c_str());
		}

		if (outCount)
			*outCount = static_cast<int>(cstrs.size());

		return cstrs.data(); // 포인터 배열 반환
	}

	EXPORT_API void SetSceneManager(void* sceneManager)
	{
		SceneManager* ptr = static_cast<SceneManager*>(sceneManager);
		auto& vector = SceneManagers->GetScenes();
		auto& exeVector = ptr->GetScenes();

		vector.clear();
		for (auto& scene : exeVector)
		{
			vector.push_back(scene);
		}

		SceneManagers->SetActiveSceneIndex(ptr->GetActiveSceneIndex());
		SceneManagers->SetActiveScene(ptr->GetActiveScene());
		SceneManagers->SetGameStart(ptr->IsGameStart());
		auto& dontDestroyOnLoadObjects = SceneManagers->GetDontDestroyOnLoadObjects();
		auto& exeDontDestroyOnLoadObjects = ptr->GetDontDestroyOnLoadObjects();
		dontDestroyOnLoadObjects.clear();
		for (auto& obj : exeDontDestroyOnLoadObjects)
		{
			dontDestroyOnLoadObjects.push_back(obj);
		}
		SceneManagers->SetActiveSceneIndex(ptr->GetActiveSceneIndex());
		SceneManagers->SetActiveScene(ptr->GetActiveScene());
		SceneManagers->SetGameStart(ptr->IsGameStart());
	}
#pragma	endregion

	EXPORT_API void InitModuleFactory()
	{
		// Register the factory function for TestBehavior Automation
		CreateFactory::GetInstance()->RegisterFactory("TestBehavior", []() { return new TestBehavior(); });
	}
}

