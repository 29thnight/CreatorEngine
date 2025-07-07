#pragma once
#include "Export.h"
#include "CreateFactory.h"
#include "SceneManager.h"

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

		for (const auto& [name, func] : CreateFactory::GetInstance()->factoryMap)
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

	EXPORT_API void SetInstanceFuncPtr(Singleton<SceneManager>::FGetInstance funcPtr)
	{
		const_cast<std::shared_ptr<SceneManager>&>(SceneManagers) = funcPtr();
	}
#pragma	endregion

	EXPORT_API void InitModuleFactory()
	{
		// Register the factory function for TestBehavior Automation
		CreateFactory::GetInstance()->RegisterFactory("GameManager", []() { return new GameManager(); });
		CreateFactory::GetInstance()->RegisterFactory("EntityItem", []() { return new EntityItem(); });
		CreateFactory::GetInstance()->RegisterFactory("EntityAsis", []() { return new EntityAsis(); });
		CreateFactory::GetInstance()->RegisterFactory("Temp", []() { return new Temp(); });
		CreateFactory::GetInstance()->RegisterFactory("Entity", []() { return new Entity(); });
		CreateFactory::GetInstance()->RegisterFactory("Player", []() { return new Player(); });
		CreateFactory::GetInstance()->RegisterFactory("TestBehavior", []() { return new TestBehavior(); });
		CreateFactory::GetInstance()->RegisterFactory("AsisMove", []() { return new AsisMove(); });
	}
}