#pragma once

#define ReflectVolumeProfile \
ReflectionField(VolumeProfile) \
{ \
	PropertyField \
	({ \
		meta_property(profileName) \
		meta_property(settings) \
	}); \
	FieldEnd(VolumeProfile, PropertyOnly) \
};
