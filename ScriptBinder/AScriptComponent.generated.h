#pragma once

#define ReflectAScriptComponent \
ReflectionFieldInheritance(AScriptComponent, Component) \
{ \
	PropertyField \
	({ \
		meta_property(m_scriptGuid) \
		meta_property(m_scriptName) \
	}); \
	FieldEnd(AScriptComponent, PropertyOnlyInheritance) \
};
