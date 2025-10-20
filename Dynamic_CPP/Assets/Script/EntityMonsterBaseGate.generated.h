#pragma once

#define ReflectEntityMonsterBaseGate \
ReflectionScriptField(EntityMonsterBaseGate) \
{ \
	PropertyField \
	({ \
		meta_property(maxHP) \
	}); \
	FieldEnd(EntityMonsterBaseGate, PropertyOnly) \
};
