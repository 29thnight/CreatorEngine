#pragma once

#define ReflectPlayer \
ReflectionScriptField(Player) \
{ \
	PropertyField \
	({ \
		meta_property(HP) \
	}); \
	FieldEnd(Player, PropertyOnly) \
};
