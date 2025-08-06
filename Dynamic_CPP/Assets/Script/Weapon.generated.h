#pragma once

#define ReflectWeapon \
ReflectionScriptField(Weapon) \
{ \
	PropertyField \
	({ \
		meta_property(itemtype) \
	}); \
	FieldEnd(Weapon, PropertyOnly) \
};
