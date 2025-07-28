#pragma once

#define ReflectInputAction \
ReflectionField(InputAction) \
{ \
	PropertyField \
	({ \
		meta_property(actionName) \
		meta_property(m_scriptName) \
		meta_property(funName) \
	}); \
	FieldEnd(InputAction, PropertyOnly) \
};
