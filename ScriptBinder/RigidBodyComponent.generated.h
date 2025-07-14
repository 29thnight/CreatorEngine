#pragma once

#define ReflectRigidBodyComponent \
ReflectionFieldInheritance(RigidBodyComponent, Component) \
{ \
	PropertyField \
	({ \
		meta_property(m_bodyType) \
		meta_property(LinearDamping) \
		meta_property(m_mass) \
		meta_property(maxLinearVelocity) \
		meta_property(maxAngularVelocity) \
		meta_property(maxContactImpulse) \
		meta_property(maxDepenetrationVelocity) \
	}); \
	FieldEnd(RigidBodyComponent, PropertyOnlyInheritance) \
};
