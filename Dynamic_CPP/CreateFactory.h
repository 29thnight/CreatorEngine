#pragma once
#include "Core.Minimal.h"
#include "Export.h"

// Automation include ScriptClass header
#include "UIHPObserver.h"
#include "SceneTag.h"
#include "BossHPBar.h"
#include "BGMAmbienceController.h"
#include "BGMChangeTrigger.h"
#include "PortalTrigger.h"
#include "ClearPortal.h"
#include "ExitPauseButton.h"
#include "GateBarrier.h"
#include "BGMController.h"
#include "PauseMenuButton.h"
#include "KoriEmoteSystem.h"
#include "BossBomb.h"
#include "MobSpawnerBound.h"
#include "MenuKeyObserver.h"
#include "ReturnMainScene.h"
#include "RestartButton.h"
#include "SceneTransitionUI.h"
#include "TestShader.h"
#include "SettingWindowUI.h"
#include "ExitButton.h"
#include "CreditsButton.h"
#include "SettingButton.h"
#include "TutorialButton.h"
#include "StartButton.h"
#include "BootstrapObserver.h"
#include "EntityMonsterTower.h"
#include "ViveSwitchUI.h"
#include "SoundBarUI.h"
#include "SwordProjectileEffect.h"
#include "SwordProjectile.h"
#include "ControllerVibration.h"
#include "ObjectPoolManager.h"
#include "ReachEventTrigger.h"
#include "BP001.h"
#include "EntityMonsterBaseGate.h"
#include "PlayEffectAll.h"
#include "SwordHitEffect.h"
#include "BP003.h"
#include "SFXPoolManager.h"
#include "SelectTimer.h"
#include "ImageSlideshow.h"
#include "IllustrationMove.h"
#include "MonEleteProjetile.h"
#include "TestEffect.h"
#include "ItemComponent.h"
#include "ItemManager.h"
#include "ItemUIPopup.h"
#include "ItemUIIcon.h"
#include "EntityEleteMonster.h"
#include "GameInit.h"
#include "ImageButton.h"
#include "SlashEffect.h"
#include "PlayerSelector.h"
#include "InputDeviceDetector.h"
#include "ItemPopup.h"
#include "MovingUILayer.h"
#include "MobSpawner.h"
#include "SwitchingSceneTrigger.h"
#include "LoadingController.h"
#include "WeaponSlotDurFont.h"
#include "EventTarget.h"
#include "MonsterProjectile.h"
#include "TestMonsterB.h"
#include "EventManager.h"
#include "HPBar.h"
#include "EventSelector.h"
#include "RewardObserver.h"
#include "EntityBigWood.h"
#include "CriticalMark.h"
#include "EntityItemHeal.h"
#include "WeaponSlotController.h"
#include "WeaponSlot.h"
#include "EntityRock.h"
#include "EntitySpiritStone.h"
#include "UIGageTest.h"
#include "EntityMonsterA.h"
#include "WeaponCapsule.h"
#include "Explosion.h"
#include "Bomb.h"
#include "NormalBullet.h"
#include "TBoss1.h"
#include "CurveIndicator.h"
#include "SpecialBullet.h"
#include "Bullet.h"
#include "DestroyEffect.h"
#include "EntityEnemy.h"
#include "Weapon.h"
#include "TweenManager.h"
#include "EntityResource.h"
#include "TestEnemy.h"
#include "InverseKinematic.h"
#include "TestTreeBehavior.h"
#include "GameManager.h"
#include "EntityItem.h"
#include "EntityAsis.h"
#include "Entity.h"
#include "Player.h"
#include "TestBehavior.h"
#include "CameraMove.h"
class CreateFactory : public Singleton<CreateFactory>
{
private:
	friend class Singleton;
	CreateFactory() = default;
	~CreateFactory() = default;
public:
	// Register a factory function for creating ModuleBehavior instances
	void RegisterFactory(const std::string& className, std::function<ModuleBehavior*()> factoryFunction)
	{
		if (factoryMap.find(className) != factoryMap.end())
		{
			std::cout << "Factory for class " << className << " is already registered." << std::endl;
			return; // or throw an exception
		}

		factoryMap[className] = factoryFunction;
	}
	// Create a ModuleBehavior instance using the registered factory function
	ModuleBehavior* CreateInstance(const std::string& className)
	{
		auto it = factoryMap.find(className);
		if (it != factoryMap.end())
		{
			return it->second();
		}
		return nullptr; // or throw an exception
	}
	std::unordered_map<std::string, std::function<ModuleBehavior*()>> factoryMap;
};
