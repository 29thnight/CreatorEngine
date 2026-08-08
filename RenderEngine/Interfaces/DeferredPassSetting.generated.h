#pragma once

#define ReflectDeferredPassSetting \
ReflectionField(DeferredPassSetting) \
{ \
	PropertyField \
	({ \
		meta_property(useAmbientOcclusion) \
		meta_property(useEnvironmentMap) \
		meta_property(useLightWithShadows) \
		meta_property(envMapIntensity) \
	}); \
	FieldEnd(DeferredPassSetting, PropertyOnly) \
};
