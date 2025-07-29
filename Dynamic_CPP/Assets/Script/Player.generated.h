#pragma once

#define ReflectPlayer \
ReflectionScriptField(Player) \
{ \
	PropertyField \
	({ \
		meta_property(playerIndex) \
		meta_property(maxHP) \
		meta_property(ThrowPowerX) \
		meta_property(ThrowPowerY) \
		meta_property(m_comboTime) \
		meta_property(m_dashPower) \
		meta_property(m_dashTime) \
		meta_property(dashCooldown) \
		meta_property(dubbleDashTime) \
		meta_property(dashAmount) \
		meta_property(AttackPowerX) \
		meta_property(AttackPowerY) \
		meta_property(detectAngle) \
		meta_property(KnockBackForceY) \
		meta_property(KnockBackForce) \
	}); \
	MethodField \
	({ \
		meta_method(Move, "dir") \
		meta_method(CatchAndThrow) \
		meta_method(Dash) \
		meta_method(StartAttack) \
		meta_method(Charging) \
		meta_method(Attack) \
		meta_method(SwapWeaponLeft) \
		meta_method(SwapWeaponRight) \
		meta_method(DeleteCurWeapon) \
		meta_method(OnPunch) \
	}); \
	FieldEnd(Player, PropertyAndMethod) \
};
