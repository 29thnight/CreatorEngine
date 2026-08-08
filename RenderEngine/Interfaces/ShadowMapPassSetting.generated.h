#pragma once

#define ReflectShadowMapPassSetting \
ReflectionField(ShadowMapPassSetting) \
{ \
	PropertyField \
	({ \
		meta_property(useCascade) \
		meta_property(isCloudOn) \
		meta_property(cloudSize) \
		meta_property(cloudDirection) \
		meta_property(cloudMoveSpeed) \
		meta_property(cloudAlpha) \
		meta_property(epsilon) \
	}); \
	FieldEnd(ShadowMapPassSetting, PropertyOnly) \
};
