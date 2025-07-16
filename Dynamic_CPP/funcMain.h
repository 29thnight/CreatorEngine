#pragma once
#include "Export.h"
#include "CreateFactory.h"
#include "BTActionFactory.h"
#include "BTConditionFactory.h"
#include "BTConditionDecoratorFactory.h"
#include "SceneManager.h"
#include "NodeFactory.h"

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

	EXPORT_API BT::ActionNode* CreateBTActionNode(const char* className)
	{
		std::string classNameStr(className);
		return ActionCreateFactory::GetInstance()->CreateInstance(classNameStr);
	}

	EXPORT_API BT::ConditionNode* CreateBTConditionNode(const char* className)
	{
		std::string classNameStr(className);
		return ConditionCreateFactory::GetInstance()->CreateInstance(classNameStr);
	}

	EXPORT_API BT::ConditionDecoratorNode* CreateBTConditionDecoratorNode(const char* className)
	{
		std::string classNameStr(className);
		return ConditionDecoratorCreateFactory::GetInstance()->CreateInstance(classNameStr);
	}

	EXPORT_API const char** ListBTActionNode(int* outCount)
	{
		static std::vector<std::string> nameVector;
		static std::vector<const char*> cstrs;
		nameVector.clear();
		cstrs.clear();
		for (const auto& [name, func] : ActionCreateFactory::GetInstance()->factoryMap)
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

	EXPORT_API const char** ListBTConditionNode(int* outCount)
	{
		static std::vector<std::string> nameVector;
		static std::vector<const char*> cstrs;
		nameVector.clear();
		cstrs.clear();
		for (const auto& [name, func] : ConditionCreateFactory::GetInstance()->factoryMap)
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

	EXPORT_API const char** ListBTConditionDecoratorNode(int* outCount)
	{
		static std::vector<std::string> nameVector;
		static std::vector<const char*> cstrs;
		nameVector.clear();
		cstrs.clear();
		for (const auto& [name, func] : ConditionDecoratorCreateFactory::GetInstance()->factoryMap)
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

	EXPORT_API void SetSceneManager(Singleton<SceneManager>::FGetInstance funcPtr)
	{
		const_cast<std::shared_ptr<SceneManager>&>(SceneManagers) = funcPtr();
	}

	EXPORT_API void SetNodeFactory(Singleton<BT::NodeFactory>::FGetInstance funcPtr)
	{
		const_cast<std::shared_ptr<BT::NodeFactory>&>(BTNodeFactory) = funcPtr();
	}

	EXPORT_API void DeleteModuleBehavior(ModuleBehavior* behavior)
	{
		if (behavior)
		{
			delete behavior;
		}
	}

	EXPORT_API void DeleteBTActionNode(BT::ActionNode* actionNode)
	{
		if (actionNode)
		{
			delete actionNode;
		}
	}

	EXPORT_API void DeleteBTConditionNode(BT::ConditionNode* conditionNode)
	{
		if (conditionNode)
		{
			delete conditionNode;
		}
	}

	EXPORT_API void DeleteBTConditionDecoratorNode(BT::ConditionDecoratorNode* conditionDecoratorNode)
	{
		if (conditionDecoratorNode)
		{
			delete conditionDecoratorNode;
		}
	}

	EXPORT_API void SetPhysicsManager(Singleton<PhysicsManager>::FGetInstance funcPtr)
	{
		const_cast<std::shared_ptr<PhysicsManager>&>(PhysicsManagers) = funcPtr();
	}

	EXPORT_API void SetPhysics(Singleton<PhysicX>::FGetInstance funcPtr)
	{
		const_cast<std::shared_ptr<PhysicX>&>(Physics) = funcPtr();
	}
#pragma	endregion

	EXPORT_API void InitModuleFactory()
	{
		// Register the factory function for TestBehavior Automation
		CreateFactory::GetInstance()->RegisterFactory("TestEnemy", []() { return new TestEnemy(); });
		CreateFactory::GetInstance()->RegisterFactory("InverseKinematic", []() { return new InverseKinematic(); });
		CreateFactory::GetInstance()->RegisterFactory("TestTreeBehavior", []() { return new TestTreeBehavior(); });
		CreateFactory::GetInstance()->RegisterFactory("Rock", []() { return new Rock(); });
		CreateFactory::GetInstance()->RegisterFactory("AsisFeed", []() { return new AsisFeed(); });
		CreateFactory::GetInstance()->RegisterFactory("GameManager", []() { return new GameManager(); });
		CreateFactory::GetInstance()->RegisterFactory("EntityItem", []() { return new EntityItem(); });
		CreateFactory::GetInstance()->RegisterFactory("EntityAsis", []() { return new EntityAsis(); });
		CreateFactory::GetInstance()->RegisterFactory("Entity", []() { return new Entity(); });
		CreateFactory::GetInstance()->RegisterFactory("Player", []() { return new Player(); });
		CreateFactory::GetInstance()->RegisterFactory("TestBehavior", []() { return new TestBehavior(); });
		CreateFactory::GetInstance()->RegisterFactory("AsisMove", []() { return new AsisMove(); });
	}

	EXPORT_API void InitActionFactory()
	{
		// Register the factory function for BTAction Automation
		ActionCreateFactory::GetInstance()->RegisterFactory("TestAction", []() { return new TestAction(); });
	}

	EXPORT_API void InitConditionFactory()
	{
		// Register the factory function for BTCondition Automation
		ConditionCreateFactory::GetInstance()->RegisterFactory("TestCon", []() { return new TestCon(); });

	}

	EXPORT_API void InitConditionDecoratorFactory()
	{
		// Register the factory function for BTConditionDecorator Automation
		ConditionDecoratorCreateFactory::GetInstance()->RegisterFactory("TestConCec", []() { return new TestConCec(); });

	}
}
