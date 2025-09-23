#pragma once

#define ReflectCameraComponent \
ReflectionFieldInheritance(CameraComponent, Component) \
{ \
	PropertyField \
	({ \
		meta_property(m_inspectorOnlyCameraPtr) \
		meta_property(m_cameraIndex) \
	}); \
	FieldEnd(CameraComponent, PropertyOnlyInheritance) \
};
