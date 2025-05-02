#pragma once

#define ReflectCamera \
ReflectionField(Camera) \
{ \
	PropertyField \
	({ \
		meta_property(rotate) \
		meta_property(m_nearPlane) \
		meta_property(m_farPlane) \
		meta_property(m_speed) \
		meta_property(m_fov) \
	}); \
	FieldEnd(Camera, PropertyOnly) \
};
