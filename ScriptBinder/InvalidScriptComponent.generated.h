#pragma once

#define ReflectInvalidScriptComponent \
ReflectionFieldInheritance(InvalidScriptComponent, Component) \
{ \
	PropertyField \
	({ \
		meta_property(m_errorMessage) \
	}); \
	FieldEnd(InvalidScriptComponent, PropertyOnlyInheritance) \
};
