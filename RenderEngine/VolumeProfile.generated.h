#pragma once

#define ReflectVolumeProfile \
ReflectionField(VolumeProfile) \
{ \
	PropertyField \
	({ \
		meta_property(settings) \
	}); \
	FieldEnd(VolumeProfile, PropertyOnly) \
};
