#pragma once

#define ReflectLightMapping \
ReflectionField(LightMapping) \
{ \
	PropertyField \
	({ \
		meta_property(lightmapIndex) \
		meta_property(ligthmapResolution) \
		meta_property(lightmapScale) \
		meta_property(lightmapOffset) \
		meta_property(lightmapTiling) \
	}); \
	FieldEnd(LightMapping, PropertyOnly) \
};
