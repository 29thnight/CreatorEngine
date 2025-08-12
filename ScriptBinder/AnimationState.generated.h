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
		meta_property(animationSpeed) \
		meta_property(multiplerAnimationSpeed) \
		meta_property(animationSpeedParameterName) \
		meta_property(useMultipler) \
	}); \
	FieldEnd(AnimationState, PropertyOnly) \
};
