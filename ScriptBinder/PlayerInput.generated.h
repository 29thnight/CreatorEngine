#pragma once

#define ReflectPlayerInput \
ReflectionFieldInheritance(PlayerInput, Component) \
{ \
	PropertyField \
	({ \
		meta_property(m_actionMapName) \
		meta_property(m_scriptName) \
		meta_property(m_funName) \
	}); \
	FieldEnd(PlayerInput, PropertyOnlyInheritance) \
};
