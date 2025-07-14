#pragma once

#define ReflectPlayer \
ReflectionScriptField(Player) \
{ \
	PropertyField \
	({ \
		meta_property(HP) \
		meta_property(ThrowPowerX) \
		meta_property(ThrowPowerY) \
	}); \
	FieldEnd(Player, PropertyOnly) \
};
