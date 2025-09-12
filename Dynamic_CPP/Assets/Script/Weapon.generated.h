#pragma once

#define ReflectWeapon \
ReflectionScriptField(Weapon) \
{ \
	PropertyField \
	({ \
		meta_property(itemType) \
		meta_property(itemAckDmg) \
		meta_property(itemAckSpd) \
		meta_property(itemAckRange) \
		meta_property(itemKnockback) \
		meta_property(coopCrit) \
		meta_property(chgTime) \
		meta_property(chgDmgscal) \
		meta_property(chgSpd) \
		meta_property(chgDelay) \
		meta_property(chgRange) \
		meta_property(chgHitbox) \
		meta_property(chgKnockback) \
		meta_property(durMax) \
		meta_property(durUseAtk) \
		meta_property(durUseChg) \
		meta_property(durUseBuf) \
	}); \
	FieldEnd(Weapon, PropertyOnly) \
};
