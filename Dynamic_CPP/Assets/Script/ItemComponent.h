#pragma once
#include "Core.Minimal.h"
#include "ModuleBehavior.h"
#include "ItemComponent.generated.h"

class ItemComponent : public ModuleBehavior
{
public:
   ReflectItemComponent
	[[ScriptReflectionField]]
	MODULE_BEHAVIOR_BODY(ItemComponent)
	virtual void Start() override;
	virtual void OnTriggerEnter(const Collision& collision) override;
	virtual void OnTriggerExit(const Collision& collision) override;
	virtual void Update(float tick) override;

	void SetItemIcon(class ItemUIIcon* comp);

public:
	[[Property]]
	int m_playerID{ -1 };
	[[Property]]
	int m_itemID{ 0 };
	[[Property]]
	int m_itemRarity{ 0 };
	std::string name{};
	std::string description{};
	int price{ 0 };
	int enhanceType{ 0 };
	int enhanceValue{ 0 };
	int m_enterCount{};
	bool m_isItemSoldOut{ false };
	bool m_isActive{ false };
	bool m_isPrevActive{};

private:
	class ItemUIIcon* m_controlIcon{};
};
