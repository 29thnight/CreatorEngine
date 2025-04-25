#pragma once

#define ReflectRigidBodyComponent \
ReflectionFieldInheritance(RigidBodyComponent, Component) \
{ \
	PropertyField \
	({ \
		meta_property(m_bodyType) \
	}); \
	FieldEnd(RigidBodyComponent, PropertyOnlyInheritance) \
};
