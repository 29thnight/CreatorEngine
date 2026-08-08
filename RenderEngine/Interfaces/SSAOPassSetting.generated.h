#pragma once

#define ReflectSSAOPassSetting \
ReflectionField(SSAOPassSetting) \
{ \
	PropertyField \
	({ \
		meta_property(radius) \
		meta_property(thickness) \
	}); \
	FieldEnd(SSAOPassSetting, PropertyOnly) \
};
