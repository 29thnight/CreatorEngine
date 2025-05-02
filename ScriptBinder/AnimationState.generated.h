#pragma once

#define ReflectAnimationState \
ReflectionField(AnimationState) \
{ \
	PropertyField \
	({ \
		meta_property(Name) \
		meta_property(Transitions) \
		meta_property(index) \
	}); \
	FieldEnd(AnimationState, PropertyOnly) \
};
