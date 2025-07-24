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
		meta_method(OnPunch) \
	}); \
	FieldEnd(Player, PropertyAndMethod) \
};
