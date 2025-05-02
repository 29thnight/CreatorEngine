#pragma once

#define ReflectCameraComponent \
ReflectionFieldInheritance(CameraComponent, Component) \
{ \
	PropertyField \
	({ \
		meta_property(m_pCamera) \
		meta_property(m_cameraIndex) \
	}); \
	FieldEnd(CameraComponent, PropertyOnlyInheritance) \
};
