#pragma once

#define ReflectBomb \
ReflectionScriptFieldInheritance(Bomb, Weapon) \
{ \
	PropertyField \
	({ \
		meta_property(throwSpeed) \
		meta_property(throwPowerY) \
		meta_property(duration) \
	}); \
	FieldEnd(Bomb, PropertyOnlyScriptInheritance) \
};
