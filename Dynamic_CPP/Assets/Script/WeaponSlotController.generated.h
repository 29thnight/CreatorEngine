#pragma once

#define ReflectWeaponSlotController \
ReflectionScriptField(WeaponSlotController) \
{ \
	PropertyField \
	({ \
		meta_property(m_playerIndex) \
	}); \
	FieldEnd(WeaponSlotController, PropertyOnly) \
};
