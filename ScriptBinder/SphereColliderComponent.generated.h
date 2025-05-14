#pragma once

#define ReflectSphereColliderComponent \
ReflectionFieldInheritance(SphereColliderComponent, Component) \
{ \
	PropertyField \
	({ \
		meta_property(m_Info) \
		meta_property(m_posOffset) \
		meta_property(m_rotOffset) \
	}); \
	FieldEnd(SphereColliderComponent, PropertyOnlyInheritance) \
};
