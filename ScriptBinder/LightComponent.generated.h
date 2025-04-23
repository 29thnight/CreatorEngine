#pragma once

#define ReflectLightComponent \
ReflectionFieldInheritance(LightComponent, Component) \
{ \
	PropertyField \
	({ \
		meta_property(m_lightIndex) \
		meta_property(m_color) \
		meta_property(m_constantAttenuation) \
		meta_property(m_linearAttenuation) \
		meta_property(m_quadraticAttenuation) \
		meta_property(m_spotLightAngle) \
		meta_property(m_lightType) \
		meta_property(m_lightStatus) \
		meta_property(m_intencity) \
	}); \
	FieldEnd(LightComponent, PropertyOnlyInheritance) \
};
