#pragma once

#define ReflectBGMChangeTrigger \
ReflectionScriptField(BGMChangeTrigger) \
{ \
	PropertyField \
	({ \
		meta_property(nextBGMTag) \
		meta_property(nextAmbienceTag) \
	}); \
	FieldEnd(BGMChangeTrigger, PropertyOnly) \
};
