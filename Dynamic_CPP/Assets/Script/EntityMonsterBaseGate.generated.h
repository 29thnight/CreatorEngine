#pragma once

#define ReflectEntityMonsterBaseGate \
ReflectionScriptField(EntityMonsterBaseGate) \
{ \
	PropertyField \
	({ \
		meta_property(maxHP) \
		meta_property(halfDestroyedHP) \
	}); \
	FieldEnd(EntityMonsterBaseGate, PropertyOnly) \
};
