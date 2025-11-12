#pragma once

#define ReflectCharacterControllerComponent \
ReflectionFieldInheritance(CharacterControllerComponent, Component) \
{ \
	PropertyField \
	({ \
		meta_property(m_posOffset) \
		meta_property(m_radius) \
		meta_property(m_rotOffset) \
		meta_property(m_height) \
		meta_property(maxSpeed) \
		meta_property(acceleration) \
		meta_property(staticFriction) \
		meta_property(dynamicFriction) \
		meta_property(jumpSpeed) \
		meta_property(gravityWeight) \
		meta_property(m_fBaseSpeed) \
		meta_property(m_fFinalMultiplierSpeed) \
		meta_property(m_rotationSpeed) \
	}); \
	FieldEnd(CharacterControllerComponent, PropertyOnlyInheritance) \
};
