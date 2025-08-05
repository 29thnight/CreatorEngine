#pragma once

#define ReflectAnimationState \
ReflectionField(AnimationState) \
{ \
	PropertyField \
	({ \
		meta_property(m_name) \
		meta_property(behaviourName) \
		meta_property(Transitions) \
		meta_property(index) \
		meta_property(AnimationIndex) \
		meta_property(m_isAny) \
	}); \
	FieldEnd(AnimationState, PropertyOnly) \
};
