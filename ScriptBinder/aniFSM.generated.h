#pragma once

#define ReflectaniFSM \
ReflectionFieldInheritance(AnimationController, Component) \
{ \
	PropertyField \
	({ \
		meta_property(CurState) \
		meta_property(StateVec) \
		meta_property(Parameters) \
	}); \
	FieldEnd(AnimationController, PropertyOnlyInheritance) \
};
