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
		CreateFactory::GetInstance()->RegisterFactory("ImageSlideshow", []() { return new ImageSlideshow(); });
		CreateFactory::GetInstance()->RegisterFactory("IllustrationMove", []() { return new IllustrationMove(); });
		CreateFactory::GetInstance()->RegisterFactory("MonEleteProjetile", []() { return new MonEleteProjetile(); });
		CreateFactory::GetInstance()->RegisterFactory("TestEffect", []() { return new TestEffect(); });
		CreateFactory::GetInstance()->RegisterFactory("ItemComponent", []() { return new ItemComponent(); });
		CreateFactory::GetInstance()->RegisterFactory("ItemManager", []() { return new ItemManager(); });
		CreateFactory::GetInstance()->RegisterFactory("ItemUIPopup", []() { return new ItemUIPopup(); });
		CreateFactory::GetInstance()->RegisterFactory("ItemUIIcon", []() { return new ItemUIIcon(); });
		CreateFactory::GetInstance()->RegisterFactory("EntityEleteMonster", []() { return new EntityEleteMonster(); });
		CreateFactory::GetInstance()->RegisterFactory("GameInit", []() { return new GameInit(); });
		CreateFactory::GetInstance()->RegisterFactory("ImageButton", []() { return new ImageButton(); });
		CreateFactory::GetInstance()->RegisterFactory("SlashEffect", []() { return new SlashEffect(); });
		CreateFactory::GetInstance()->RegisterFactory("PlayerSelector", []() { return new PlayerSelector(); });
		CreateFactory::GetInstance()->RegisterFactory("InputDeviceDetector", []() { return new InputDeviceDetector(); });
		CreateFactory::GetInstance()->RegisterFactory("ItemPopup", []() { return new ItemPopup(); });
		CreateFactory::GetInstance()->RegisterFactory("MovingUILayer", []() { return new MovingUILayer(); });
		CreateFactory::GetInstance()->RegisterFactory("MobSpawner", []() { return new MobSpawner(); });
		CreateFactory::GetInstance()->RegisterFactory("SwitchingSceneTrigger", []() { return new SwitchingSceneTrigger(); });
		CreateFactory::GetInstance()->RegisterFactory("LoadingController", []() { return new LoadingController(); });
		CreateFactory::GetInstance()->RegisterFactory("WeaponSlotDurFont", []() { return new WeaponSlotDurFont(); });
		CreateFactory::GetInstance()->RegisterFactory("EventTarget", []() { return new EventTarget(); });
		CreateFactory::GetInstance()->RegisterFactory("EventManager", []() { return new EventManager(); });
		CreateFactory::GetInstance()->RegisterFactory("HPBar", []() { return new HPBar(); });
		CreateFactory::GetInstance()->RegisterFactory("EventSelector", []() { return new EventSelector(); });
		CreateFactory::GetInstance()->RegisterFactory("RewardObserver", []() { return new RewardObserver(); });
		CreateFactory::GetInstance()->RegisterFactory("EntityBigWood", []() { return new EntityBigWood(); });
		CreateFactory::GetInstance()->RegisterFactory("CriticalMark", []() { return new CriticalMark(); });
		CreateFactory::GetInstance()->RegisterFactory("EntityItemHeal", []() { return new EntityItemHeal(); });
		CreateFactory::GetInstance()->RegisterFactory("MonsterProjectile", []() { return new MonsterProjectile(); });
		CreateFactory::GetInstance()->RegisterFactory("TestMonsterB", []() { return new TestMonsterB(); });
		CreateFactory::GetInstance()->RegisterFactory("WeaponSlotController", []() { return new WeaponSlotController(); });
		CreateFactory::GetInstance()->RegisterFactory("WeaponSlot", []() { return new WeaponSlot(); });
		CreateFactory::GetInstance()->RegisterFactory("EntityRock", []() { return new EntityRock(); });
		CreateFactory::GetInstance()->RegisterFactory("EntitySpiritStone", []() { return new EntitySpiritStone(); });
		CreateFactory::GetInstance()->RegisterFactory("EntityMonsterA", []() { return new EntityMonsterA(); });
		CreateFactory::GetInstance()->RegisterFactory("UIGageTest", []() { return new UIGageTest(); });
		CreateFactory::GetInstance()->RegisterFactory("WeaponCapsule", []() { return new WeaponCapsule(); });
		CreateFactory::GetInstance()->RegisterFactory("Explosion", []() { return new Explosion(); });
		CreateFactory::GetInstance()->RegisterFactory("Bomb", []() { return new Bomb(); });
		CreateFactory::GetInstance()->RegisterFactory("NormalBullet", []() { return new NormalBullet(); });
		CreateFactory::GetInstance()->RegisterFactory("TBoss1", []() { return new TBoss1(); });
		CreateFactory::GetInstance()->RegisterFactory("CurveIndicator", []() { return new CurveIndicator(); });
		CreateFactory::GetInstance()->RegisterFactory("SpecialBullet", []() { return new SpecialBullet(); });
		CreateFactory::GetInstance()->RegisterFactory("Bullet", []() { return new Bullet(); });
		CreateFactory::GetInstance()->RegisterFactory("DestroyEffect", []() { return new DestroyEffect(); });
		CreateFactory::GetInstance()->RegisterFactory("EntityEnemy", []() { return new EntityEnemy(); });
		CreateFactory::GetInstance()->RegisterFactory("Weapon", []() { return new Weapon(); });
		CreateFactory::GetInstance()->RegisterFactory("TweenManager", []() { return new TweenManager(); });
		CreateFactory::GetInstance()->RegisterFactory("EntityResource", []() { return new EntityResource(); });
		CreateFactory::GetInstance()->RegisterFactory("TestEnemy", []() { return new TestEnemy(); });
		CreateFactory::GetInstance()->RegisterFactory("InverseKinematic", []() { return new InverseKinematic(); });
		CreateFactory::GetInstance()->RegisterFactory("TestTreeBehavior", []() { return new TestTreeBehavior(); });
		CreateFactory::GetInstance()->RegisterFactory("GameManager", []() { return new GameManager(); });
		CreateFactory::GetInstance()->RegisterFactory("EntityItem", []() { return new EntityItem(); });
		CreateFactory::GetInstance()->RegisterFactory("EntityAsis", []() { return new EntityAsis(); });
		CreateFactory::GetInstance()->RegisterFactory("Entity", []() { return new Entity(); });
		CreateFactory::GetInstance()->RegisterFactory("Player", []() { return new Player(); });
		CreateFactory::GetInstance()->RegisterFactory("TestBehavior", []() { return new TestBehavior(); });
		CreateFactory::GetInstance()->RegisterFactory("CameraMove", []() { return new CameraMove(); });
	}

	EXPORT_API void InitActionFactory()
	{
		// Register the factory function for BTAction Automation
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
