#pragma once

#define ReflectaniFSM \
ReflectionFieldInheritance(aniFSM, Component) \
{ \
	PropertyField \
	({ \
		meta_property(abc) \
	}); \
	FieldEnd(aniFSM, PropertyOnlyInheritance) \
};
