#pragma once

#define ReflectPlayer \
ReflectionScriptField(Player) \
{ \
	PropertyField \
	({ \
		meta_property(maxHP) \
		meta_property(ThrowPowerX) \
		meta_property(ThrowPowerY) \
		meta_property(m_comboTime) \
		meta_property(m_dashPower) \
		meta_property(dashCooldown) \
		meta_property(dubbleDashTime) \
		meta_property(dashAmount) \
		meta_property(KnockbackPowerX) \
		meta_property(KnockbackPowerY) \
		meta_property(KnockBackForceY) \
	}); \
	MethodField \
	({ \
		meta_method(OnPunch) \
	}); \
	FieldEnd(Player, PropertyAndMethod) \
};
