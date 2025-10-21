#pragma once

#define ReflectEntityMonsterTower \
ReflectionScriptField(EntityMonsterTower) \
{ \
	PropertyField \
	({ \
		meta_property(maxHP) \
		meta_property(attackRange) \
	}); \
	FieldEnd(EntityMonsterTower, PropertyOnly) \
};
