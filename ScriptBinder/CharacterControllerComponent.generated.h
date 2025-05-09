#pragma once

#define ReflectCharacterControllerComponent \
ReflectionFieldInheritance(CharacterControllerComponent, Component) \
{ \
	PropertyField \
	({ \
		meta_property(m_posOffset) \
		meta_property(m_rotOffset) \
	}); \
	FieldEnd(CharacterControllerComponent, PropertyOnlyInheritance) \
};
