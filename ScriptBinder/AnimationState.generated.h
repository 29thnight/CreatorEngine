#pragma once

#define ReflectAnimationState \
ReflectionField(AnimationState) \
{ \
	PropertyField \
	({ \
		meta_property(Name) \
		meta_property(Transitions) \
		meta_property(index) \
		meta_property(AnimationIndex) \
		meta_property(m_isAny) \
		meta_property(m_isLoop) \
	}); \
	FieldEnd(AnimationState, PropertyOnly) \
};
