#pragma once

#define ReflectColorGradingPassSetting \
ReflectionField(ColorGradingPassSetting) \
{ \
	PropertyField \
	({ \
		meta_property(isOn) \
		meta_property(lerp) \
		meta_property(textureFilePath) \
	}); \
	FieldEnd(ColorGradingPassSetting, PropertyOnly) \
};
