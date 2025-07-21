#pragma once

#define ReflectVignettePassSetting \
ReflectionField(VignettePassSetting) \
{ \
	PropertyField \
	({ \
		meta_property(isOn) \
		meta_property(radius) \
		meta_property(softness) \
	}); \
	FieldEnd(VignettePassSetting, PropertyOnly) \
};
