#pragma once

#define ReflectVolumeComponent \
ReflectionFieldInheritance(VolumeComponent, Component) \
{ \
	PropertyField \
	({ \
		meta_property(m_volumeProfileName) \
		meta_property(m_volumeProfileGuid) \
	}); \
	FieldEnd(VolumeComponent, PropertyOnlyInheritance) \
};
