#pragma once

#define ReflectWeaponCapsule \
ReflectionScriptField(WeaponCapsule) \
{ \
	PropertyField \
	({ \
		meta_property(boundingRange) \
		meta_property(boundSpeed) \
	}); \
	FieldEnd(WeaponCapsule, PropertyOnly) \
};
