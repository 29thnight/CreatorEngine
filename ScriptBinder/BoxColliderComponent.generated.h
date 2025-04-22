#pragma once

#define ReflectBoxColliderComponent \
ReflectionFieldInheritance(BoxColliderComponent, Component) \
{ \
	PropertyField \
	({ \
		meta_property(m_sizeX) \
		meta_property(m_sizeY) \
		meta_property(m_sizeZ) \
		meta_property(m_isTrigger) \
		meta_property(m_offsetX) \
		meta_property(m_offsetY) \
		meta_property(m_offsetZ) \
	}); \
	FieldEnd(BoxColliderComponent, PropertyOnlyInheritance) \
};
