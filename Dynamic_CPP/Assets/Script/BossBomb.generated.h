#pragma once

#define ReflectBossBomb \
ReflectionScriptField(BossBomb) \
{ \
	PropertyField \
	({ \
		meta_property(maxTime) \
		meta_property(maxScale) \
		meta_property(scaleFrequency) \
		meta_property(rotFrequency) \
		meta_property(flashFrequency) \
		meta_property(timeScale) \
		meta_property(explosionRadius) \
		meta_property(explosionDamage) \
	}); \
	FieldEnd(BossBomb, PropertyOnly) \
};
