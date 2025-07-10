#pragma once

#define ReflectRigidBodyComponent \
ReflectionFieldInheritance(RigidBodyComponent, Component) \
{ \
	PropertyField \
	({ \
		meta_property(m_bodyType) \
		meta_property(LinearDamping) \
		meta_property(m_mass) \
	}); \
	FieldEnd(RigidBodyComponent, PropertyOnlyInheritance) \
};
