#pragma once

#define ReflectMobSpawner \
ReflectionScriptField(MobSpawner) \
{ \
	PropertyField \
	({ \
		meta_property(mobPrefabNames) \
		meta_property(mobcounts) \
		meta_property(m_eventId) \
		meta_property(m_runtimeTag) \
		meta_property(spawnRadius) \
	}); \
	MethodField \
	({ \
		meta_method(TestSpawn) \
	}); \
	FieldEnd(MobSpawner, PropertyAndMethod) \
};
