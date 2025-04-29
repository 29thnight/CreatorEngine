#pragma once

#define ReflectaniFSM \
ReflectionFieldInheritance(aniFSM, Component) \
{ \
	PropertyField \
	({ \
		meta_property(CurState) \
		meta_property(StateVec) \
		meta_property(Parameters) \
		meta_property(animator) \
	}); \
	FieldEnd(aniFSM, PropertyOnlyInheritance) \
};
