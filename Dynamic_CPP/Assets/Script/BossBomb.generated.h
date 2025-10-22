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
	}); \
	FieldEnd(BossBomb, PropertyOnly) \
};
