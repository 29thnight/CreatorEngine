#pragma once

#define ReflectCharacterControllerComponent \
ReflectionFieldInheritance(CharacterControllerComponent, Component) \
{ \
	PropertyField \
	({ \
		meta_property(m_posOffset) \
		meta_property(m_rotOffset) \
		meta_property(m_fBaseSpeed) \
		meta_property(m_fFinalMultiplierSpeed) \
		meta_property(m_rotationSpeed) \
	}); \
	FieldEnd(CharacterControllerComponent, PropertyOnlyInheritance) \
};
