#pragma once

#define ReflectCameraComponent \
ReflectionFieldInheritance(CameraComponent, Component) \
{ \
	PropertyField \
	({ \
		meta_property(m_Camera) \
		meta_property(m_cameraIndex) \
	}); \
	FieldEnd(CameraComponent, PropertyOnlyInheritance) \
};
