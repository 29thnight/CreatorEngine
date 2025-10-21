#pragma once

#define ReflectTestShader \
ReflectionScriptField(TestShader) \
{ \
	PropertyField \
	({ \
		meta_property(lerpValue) \
		meta_property(maxScale) \
		meta_property(scaleFrequency) \
		meta_property(rotFrequency) \
		meta_property(flashStrength) \
		meta_property(flashFrequency) \
		meta_property(timeScale) \
	}); \
	FieldEnd(TestShader, PropertyOnly) \
};
