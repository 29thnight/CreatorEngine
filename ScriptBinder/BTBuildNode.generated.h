#pragma once

#define ReflectBTBuildNode \
ReflectionField(BTBuildNode) \
{ \
	PropertyField \
	({ \
		meta_property(ID) \
		meta_property(Type) \
		meta_property(Name) \
		meta_property(ParentID) \
		meta_property(IsRoot) \
		meta_property(HasScript) \
		meta_property(ScriptName) \
		meta_property(Policy) \
		meta_property(Children) \
		meta_property(Position) \
	}); \
	FieldEnd(BTBuildNode, PropertyOnly) \
};
