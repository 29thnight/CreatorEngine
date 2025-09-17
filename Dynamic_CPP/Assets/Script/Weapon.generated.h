#pragma once

#define ReflectWeapon \
ReflectionScriptField(Weapon) \
{ \
	PropertyField \
	({ \
		meta_property(itemType) \
		meta_property(itemAckDmg) \
		meta_property(itemAckRange) \
		meta_property(itemKnockback) \
		meta_property(coopCrit) \
		meta_property(chgTime) \
		meta_property(chgAckDmg) \
		meta_property(chgRange) \
		meta_property(chgKnockback) \
		meta_property(ChargeAttackBulletCount) \
		meta_property(ChargeAttackBulletAngle) \
		meta_property(durMax) \
		meta_property(durUseAtk) \
	}); \
	FieldEnd(Weapon, PropertyOnly) \
};
