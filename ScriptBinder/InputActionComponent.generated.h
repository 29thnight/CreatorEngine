#pragma once

#define ReflectInputActionComponent \
ReflectionFieldInheritance(InputActionComponent, Component) \
{ \
	PropertyField \
	({ \
		meta_property(m_actionMaps) \
	}); \
	FieldEnd(InputActionComponent, PropertyOnlyInheritance) \
};
