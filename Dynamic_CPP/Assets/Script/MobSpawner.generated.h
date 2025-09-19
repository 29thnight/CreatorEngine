#pragma once

#define ReflectMobSpawner \
ReflectionScriptField(MobSpawner) \
{ \
	PropertyField \
	({ \
		meta_property(mobPrefabNames) \
		meta_property(mobcounts) \
		meta_property(spawnRadius) \
	}); \
	MethodField \
	({ \
		meta_method(TestSpawn) \
	}); \
	FieldEnd(MobSpawner, PropertyAndMethod) \
};
