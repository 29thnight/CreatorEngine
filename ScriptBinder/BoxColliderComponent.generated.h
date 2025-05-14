#pragma once

#define ReflectBoxColliderComponent \
ReflectionFieldInheritance(BoxColliderComponent, Component) \
{ \
	PropertyField \
	({ \
		meta_property(m_Info) \
		meta_property(m_posOffset) \
		meta_property(m_rotOffset) \
	}); \
	FieldEnd(BoxColliderComponent, PropertyOnlyInheritance) \
};
