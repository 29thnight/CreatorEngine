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
		meta_property(m_useGravity) \
		meta_property(m_setTrigger) \
		meta_property(m_setKinematic) \
		meta_property(m_collisionEnabled) \
	}); \
	FieldEnd(RigidBodyComponent, PropertyOnlyInheritance) \
};
