#pragma once

#define ReflectPlayerInputComponent \
ReflectionFieldInheritance(PlayerInputComponent, Component) \
{ \
	PropertyField \
	({ \
		meta_property(controllerIndex) \
		meta_property(m_actionMapName) \
		meta_property(m_scriptName) \
	}); \
	FieldEnd(PlayerInputComponent, PropertyOnlyInheritance) \
};
