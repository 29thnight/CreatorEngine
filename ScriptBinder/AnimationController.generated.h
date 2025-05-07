#pragma once

#define ReflectAnimationController \
ReflectionField(AnimationController) \
{ \
	PropertyField \
	({ \
		meta_property(name) \
		meta_property(m_curState) \
		meta_property(StateVec) \
	}); \
	FieldEnd(AnimationController, PropertyOnly) \
};
