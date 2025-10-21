#pragma once

#define ReflectPlayerInputComponent \
ReflectionFieldInheritance(PlayerInputComponent, Component) \
{ \
	PropertyField \
	({ \
		meta_property(m_actionMapName) \
		meta_property(m_scriptName) \
		meta_property(controllerIndex) \
	}); \
	FieldEnd(PlayerInputComponent, PropertyOnlyInheritance) \
};
