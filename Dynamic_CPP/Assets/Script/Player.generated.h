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
		meta_property(m_dashCooldown) \
		meta_property(m_dubbleDashTime) \
		meta_property(m_maxDashCount) \
	}); \
	MethodField \
	({ \
		meta_method(OnPunch) \
	}); \
	FieldEnd(Player, PropertyAndMethod) \
};
