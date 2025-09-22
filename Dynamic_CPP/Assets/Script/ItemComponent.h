#pragma once
#include "Core.Minimal.h"
#include "ModuleBehavior.h"

class ItemComponent : public ModuleBehavior
{
public:
	[[ScriptReflectionField]]
	MODULE_BEHAVIOR_BODY(ItemComponent)
	virtual void Start() override;
	virtual void Update(float tick) override;

public:
	[[Property]]
	int m_itemID{ 0 };
	[[Property]]
	int m_itemRarity{ 0 };
	std::string name{};
	std::string description{};
	int price{ 0 };
	int enhanceType{ 0 };
	int enhanceValue{ 0 };
};
