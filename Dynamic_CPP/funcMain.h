#pragma once
#include "Export.h"
#include "CreateFactory.h"
#include "BTActionFactory.h"
#include "BTConditionFactory.h"
#include "BTConditionDecoratorFactory.h"
#include "SceneManager.h"
#include "NodeFactory.h"
#include "AniBehaviorFactory.h"

extern "C"
{
#pragma region Exported Functions
#pragma region Module Behavior Functions
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

		return cstrs.data(); // ������ �迭 ��ȯ
	}
#pragma endregion
#pragma region Behavior Tree Node Functions
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
		return cstrs.data(); // ������ �迭 ��ȯ
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
		return cstrs.data(); // ������ �迭 ��ȯ
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
		return cstrs.data(); // ������ �迭 ��ȯ
	}
#pragma endregion
#pragma region Animation Behavior Functions
	EXPORT_API AniBehavior* CreateAniBehavior(const char* className)
	{
		std::string classNameStr(className);
		return AniBehaviorFactory::GetInstance()->CreateInstance(classNameStr);
	}

	EXPORT_API const char** ListAniBehavior(int* outCount)
	{
		static std::vector<std::string> nameVector;
		static std::vector<const char*> cstrs;
		nameVector.clear();
		cstrs.clear();
		for (const auto& [name, func] : AniBehaviorFactory::GetInstance()->factoryMap)
		{
			nameVector.push_back(name);
		}
		for (auto& name : nameVector)
		{
			cstrs.push_back(name.c_str());
		}
		if (outCount)
			*outCount = static_cast<int>(cstrs.size());
		return cstrs.data(); // ������ �迭 ��ȯ
	}
#pragma endregion
#pragma region Memory Management Functions
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

	EXPORT_API void DeleteAniBehavior(AniBehavior* aniBehavior)
	{
		if (aniBehavior)
		{
			delete aniBehavior;
		}
	}
#pragma endregion

#pragma	endregion

#pragma region Initialization Functions
	EXPORT_API void InitModuleFactory()
	{
		// Register the factory function for TestBehavior Automation
		CreateFactory::GetInstance()->RegisterFactory("TestEffect", []() { return new TestEffect(); });
	}

	EXPORT_API void InitActionFactory()
	{
		// Register the factory function for BTAction Automation
		ActionCreateFactory::GetInstance()->RegisterFactory("BurrowMove", []() { return new BurrowMove(); });
		ActionCreateFactory::GetInstance()->RegisterFactory("BP0014", []() { return new BP0014(); });
		ActionCreateFactory::GetInstance()->RegisterFactory("BP0013", []() { return new BP0013(); });
		ActionCreateFactory::GetInstance()->RegisterFactory("BP0022", []() { return new BP0022(); });
		ActionCreateFactory::GetInstance()->RegisterFactory("BP0021", []() { return new BP0021(); });
		ActionCreateFactory::GetInstance()->RegisterFactory("BP0012", []() { return new BP0012(); });
		ActionCreateFactory::GetInstance()->RegisterFactory("BP0011", []() { return new BP0011(); });
		ActionCreateFactory::GetInstance()->RegisterFactory("BP0034", []() { return new BP0034(); });
		ActionCreateFactory::GetInstance()->RegisterFactory("BP0033", []() { return new BP0033(); });
		ActionCreateFactory::GetInstance()->RegisterFactory("BP0032", []() { return new BP0032(); });
		ActionCreateFactory::GetInstance()->RegisterFactory("BP0031", []() { return new BP0031(); });
		ActionCreateFactory::GetInstance()->RegisterFactory("WaitAction", []() { return new WaitAction(); });
		ActionCreateFactory::GetInstance()->RegisterFactory("KnockBackAction", []() { return new KnockBackAction(); });
		ActionCreateFactory::GetInstance()->RegisterFactory("DetectAndTargetingAction", []() { return new DetectAndTargetingAction(); });
		ActionCreateFactory::GetInstance()->RegisterFactory("BTEntityInitAction", []() { return new BTEntityInitAction(); });
		ActionCreateFactory::GetInstance()->RegisterFactory("GroggyAction", []() { return new GroggyAction(); });
		ActionCreateFactory::GetInstance()->RegisterFactory("BP005Breath", []() { return new BP005Breath(); });
		ActionCreateFactory::GetInstance()->RegisterFactory("BP004DeathWorm", []() { return new BP004DeathWorm(); });
		ActionCreateFactory::GetInstance()->RegisterFactory("BP003Impale", []() { return new BP003Impale(); });
		ActionCreateFactory::GetInstance()->RegisterFactory("BP002FireBall", []() { return new BP002FireBall(); });
		ActionCreateFactory::GetInstance()->RegisterFactory("BP001BodyAtack", []() { return new BP001BodyAtack(); });
		ActionCreateFactory::GetInstance()->RegisterFactory("BossIdleAction", []() { return new BossIdleAction(); });
		ActionCreateFactory::GetInstance()->RegisterFactory("MageActtack", []() { return new MageActtack(); });
		ActionCreateFactory::GetInstance()->RegisterFactory("RetreatAction", []() { return new RetreatAction(); });
		ActionCreateFactory::GetInstance()->RegisterFactory("TeleportAction", []() { return new TeleportAction(); });
		ActionCreateFactory::GetInstance()->RegisterFactory("DamegeAction", []() { return new DamegeAction(); });
		ActionCreateFactory::GetInstance()->RegisterFactory("ChaseAction", []() { return new ChaseAction(); });
		ActionCreateFactory::GetInstance()->RegisterFactory("Idle", []() { return new Idle(); });
		ActionCreateFactory::GetInstance()->RegisterFactory("AtteckAction", []() { return new AtteckAction(); });
		ActionCreateFactory::GetInstance()->RegisterFactory("DaedAction", []() { return new DaedAction(); });
		ActionCreateFactory::GetInstance()->RegisterFactory("TestAction", []() { return new TestAction(); });
	}

	EXPORT_API void InitConditionFactory()
	{
		// Register the factory function for BTCondition Automation
		ConditionCreateFactory::GetInstance()->RegisterFactory("IsChase", []() { return new IsChase(); });
		ConditionCreateFactory::GetInstance()->RegisterFactory("IsGroggy", []() { return new IsGroggy(); });
		ConditionCreateFactory::GetInstance()->RegisterFactory("IsTeleport", []() { return new IsTeleport(); });
		ConditionCreateFactory::GetInstance()->RegisterFactory("IsMageAttack", []() { return new IsMageAttack(); });
		ConditionCreateFactory::GetInstance()->RegisterFactory("IsReteat", []() { return new IsReteat(); });
		ConditionCreateFactory::GetInstance()->RegisterFactory("IsDetect", []() { return new IsDetect(); });
		ConditionCreateFactory::GetInstance()->RegisterFactory("IsAtteck", []() { return new IsAtteck(); });
		ConditionCreateFactory::GetInstance()->RegisterFactory("IsDaed", []() { return new IsDaed(); });
		ConditionCreateFactory::GetInstance()->RegisterFactory("TestCon", []() { return new TestCon(); });

	}

	EXPORT_API void InitConditionDecoratorFactory()
	{
		// Register the factory function for BTConditionDecorator Automation
		ConditionDecoratorCreateFactory::GetInstance()->RegisterFactory("ActionCountCheck", []() { return new ActionCountCheck(); });
		ConditionDecoratorCreateFactory::GetInstance()->RegisterFactory("IsHazadPatten", []() { return new IsHazadPatten(); });
		ConditionDecoratorCreateFactory::GetInstance()->RegisterFactory("IsUseAsis", []() { return new IsUseAsis(); });
		ConditionDecoratorCreateFactory::GetInstance()->RegisterFactory("IsStartBoss", []() { return new IsStartBoss(); });
		ConditionDecoratorCreateFactory::GetInstance()->RegisterFactory("IsInitialize", []() { return new IsInitialize(); });
		ConditionDecoratorCreateFactory::GetInstance()->RegisterFactory("IsBossRangeAttack", []() { return new IsBossRangeAttack(); });
		ConditionDecoratorCreateFactory::GetInstance()->RegisterFactory("IsBossAtteck", []() { return new IsBossAtteck(); });
		ConditionDecoratorCreateFactory::GetInstance()->RegisterFactory("BP2IsPatten", []() { return new BP2IsPatten(); });
		ConditionDecoratorCreateFactory::GetInstance()->RegisterFactory("BP1IsPatten", []() { return new BP1IsPatten(); });
		ConditionDecoratorCreateFactory::GetInstance()->RegisterFactory("Phase3", []() { return new Phase3(); });
		ConditionDecoratorCreateFactory::GetInstance()->RegisterFactory("Phase2", []() { return new Phase2(); });
		ConditionDecoratorCreateFactory::GetInstance()->RegisterFactory("Phase1", []() { return new Phase1(); });
		ConditionDecoratorCreateFactory::GetInstance()->RegisterFactory("IsDamege", []() { return new IsDamege(); });
		ConditionDecoratorCreateFactory::GetInstance()->RegisterFactory("TestConCec", []() { return new TestConCec(); });

	}

	EXPORT_API void InitAniBehaviorFactory()
	{
		// Register the factory function for AniBehavior Automation
		AniBehaviorFactory::GetInstance()->RegisterFactory("BossMeleeAni", []() { return new BossMeleeAni(); });
		AniBehaviorFactory::GetInstance()->RegisterFactory("BossRangedAni", []() { return new BossRangedAni(); });
		AniBehaviorFactory::GetInstance()->RegisterFactory("BossWarningAni", []() { return new BossWarningAni(); });
		AniBehaviorFactory::GetInstance()->RegisterFactory("BossPopupAni", []() { return new BossPopupAni(); });
		AniBehaviorFactory::GetInstance()->RegisterFactory("BossBurrowAni", []() { return new BossBurrowAni(); });
		AniBehaviorFactory::GetInstance()->RegisterFactory("PlayerRangeAttackSpecial", []() { return new PlayerRangeAttackSpecial(); });
		AniBehaviorFactory::GetInstance()->RegisterFactory("PlayerRangeAttackEnd", []() { return new PlayerRangeAttackEnd(); });
		AniBehaviorFactory::GetInstance()->RegisterFactory("PlayerRangeAttackReady", []() { return new PlayerRangeAttackReady(); });
		AniBehaviorFactory::GetInstance()->RegisterFactory("PlayerThrow", []() { return new PlayerThrow(); });
		AniBehaviorFactory::GetInstance()->RegisterFactory("PlayerGrab", []() { return new PlayerGrab(); });
		AniBehaviorFactory::GetInstance()->RegisterFactory("PlayerBombCharing", []() { return new PlayerBombCharing(); });
		AniBehaviorFactory::GetInstance()->RegisterFactory("PlayerHit", []() { return new PlayerHit(); });
		AniBehaviorFactory::GetInstance()->RegisterFactory("PlayerDash", []() { return new PlayerDash(); });
		AniBehaviorFactory::GetInstance()->RegisterFactory("PlayerStun", []() { return new PlayerStun(); });
		AniBehaviorFactory::GetInstance()->RegisterFactory("PlayerBombAttack", []() { return new PlayerBombAttack(); });
		AniBehaviorFactory::GetInstance()->RegisterFactory("PlayerRangeAttack", []() { return new PlayerRangeAttack(); });
		AniBehaviorFactory::GetInstance()->RegisterFactory("MosterMeleeAni", []() { return new MosterMeleeAni(); });
		AniBehaviorFactory::GetInstance()->RegisterFactory("PlayerAttackAH", []() { return new PlayerAttackAH(); });

	}
#pragma endregion
}
