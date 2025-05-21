#pragma once

#define ReflectCapsuleColliderComponent \
ReflectionFieldInheritance(CapsuleColliderComponent, Component) \
{ \
	PropertyField \
	({ \
		meta_property(m_radius) \
		meta_property(m_height) \
		meta_property(m_posOffset) \
		meta_property(m_rotOffset) \
	}); \
	FieldEnd(CapsuleColliderComponent, PropertyOnlyInheritance) \
};
