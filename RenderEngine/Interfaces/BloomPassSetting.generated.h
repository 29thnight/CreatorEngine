#pragma once

#define ReflectBloomPassSetting \
ReflectionField(BloomPassSetting) \
{ \
	PropertyField \
	({ \
		meta_property(applyBloom) \
		meta_property(threshold) \
		meta_property(knee) \
		meta_property(coefficient) \
		meta_property(blurRadius) \
		meta_property(blurSigma) \
	}); \
	FieldEnd(BloomPassSetting, PropertyOnly) \
};
