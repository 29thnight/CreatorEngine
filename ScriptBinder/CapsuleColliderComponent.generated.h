#pragma once

#define ReflectCapsuleColliderComponent \
ReflectionFieldInheritance(CapsuleColliderComponent, Component) \
{ \
	PropertyField \
	({ \
		meta_property(m_radius) \
		meta_property(m_posOffset) \
		meta_property(m_rotOffset) \
		meta_property(m_height) \
		meta_property(staticFriction) \
		meta_property(dynamicFriction) \
		meta_property(restitution) \
		meta_property(density) \
	}); \
	FieldEnd(CapsuleColliderComponent, PropertyOnlyInheritance) \
};
