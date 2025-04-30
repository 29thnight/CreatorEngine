#pragma once

#define ReflectAnimationController \
ReflectionField(AnimationController) \
{ \
	PropertyField \
	({ \
		meta_property(m_curState) \
		meta_property(StateVec) \
		meta_property(Parameters) \
	}); \
	FieldEnd(AnimationController, PropertyOnly) \
};
