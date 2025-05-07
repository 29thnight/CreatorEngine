#pragma once

#define ReflectModuleBehavior \
ReflectionFieldInheritance(ModuleBehavior, Component) \
{ \
	PropertyField \
	({ \
		meta_property(m_scriptGuid) \
	}); \
	FieldEnd(ModuleBehavior, PropertyOnlyInheritance) \
};
