#pragma once

#define ReflectToneMapPassSetting \
ReflectionField(ToneMapPassSetting) \
{ \
	PropertyField \
	({ \
		meta_property(isAbleAutoExposure) \
		meta_property(isAbleToneMap) \
		meta_property(fNumber) \
		meta_property(shutterTime) \
		meta_property(ISO) \
		meta_property(exposureCompensation) \
		meta_property(speedBrightness) \
		meta_property(speedDarkness) \
		meta_property(toneMapType) \
		meta_property(filmSlope) \
		meta_property(filmToe) \
		meta_property(filmShoulder) \
		meta_property(filmBlackClip) \
		meta_property(filmWhiteClip) \
		meta_property(toneMapExposure) \
	}); \
	FieldEnd(ToneMapPassSetting, PropertyOnly) \
};
