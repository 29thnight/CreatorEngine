#pragma once

#define ReflectBoxColliderComponent \
ReflectionFieldInheritance(BoxColliderComponent, Component) \
{ \
	PropertyField \
	({ \
		meta_property(m_boxExtent) \
		meta_property(m_posOffset) \
		meta_property(m_rotOffset) \
		meta_property(staticFriction) \
		meta_property(dynamicFriction) \
		meta_property(restitution) \
		meta_property(density) \
	}); \
	FieldEnd(BoxColliderComponent, PropertyOnlyInheritance) \
};
