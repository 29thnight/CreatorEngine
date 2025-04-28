#pragma once

#define ReflectaniFSM \
ReflectionFieldInheritance(aniFSM, Component) \
{ \
	PropertyField \
	({ \
		meta_property(States) \
		meta_property(Transitions) \
		meta_property(abc) \
		meta_property(curName) \
	}); \
	FieldEnd(aniFSM, PropertyOnlyInheritance) \
};
