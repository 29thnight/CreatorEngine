#pragma once
#include "Core.Minimal.h"
#include "ModuleBehavior.h"

class WeaponSlotDurFont : public ModuleBehavior
{
public:
	MODULE_BEHAVIOR_BODY(WeaponSlotDurFont)
	virtual void Start() override;
	virtual void Update(float tick) override;

private:
	class WeaponSlot* m_weaponSlot{ nullptr };
	class TextComponent* m_textComponent{ nullptr };
};
