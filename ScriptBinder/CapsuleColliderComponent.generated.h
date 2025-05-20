#pragma once

#define ReflectCapsuleColliderComponent \
ReflectionFieldInheritance(CapsuleColliderComponent, Component) \
{ \
	PropertyField \
	({ \
		meta_property(m_Info) \
		meta_property(m_posOffset) \
		meta_property(m_rotOffset) \
	}); \
	FieldEnd(CapsuleColliderComponent, PropertyOnlyInheritance) \
};
