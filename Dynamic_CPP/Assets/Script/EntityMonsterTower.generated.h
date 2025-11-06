#pragma once

#define ReflectEntityMonsterTower \
ReflectionScriptField(EntityMonsterTower) \
{ \
	PropertyField \
	({ \
		meta_property(maxHP) \
		meta_property(attackRange) \
		meta_property(attackSpeed) \
	}); \
	FieldEnd(EntityMonsterTower, PropertyOnly) \
};
