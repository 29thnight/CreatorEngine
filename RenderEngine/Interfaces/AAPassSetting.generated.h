#pragma once

#define ReflectAAPassSetting \
ReflectionField(AAPassSetting) \
{ \
	PropertyField \
	({ \
		meta_property(isApply) \
		meta_property(bias) \
		meta_property(biasMin) \
		meta_property(spanMax) \
	}); \
	FieldEnd(AAPassSetting, PropertyOnly) \
};
