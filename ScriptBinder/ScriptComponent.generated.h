#pragma once

#define ReflectScriptComponent \
ReflectionFieldInheritance(ScriptComponent, Component) \
{ \
	PropertyField \
	({ \
		meta_property(m_scriptType) \
		meta_property(m_fieldData) \
	}); \
	FieldEnd(ScriptComponent, PropertyOnlyInheritance) \
};
