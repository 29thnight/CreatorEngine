#pragma once

#define ReflectAnimationController \
ReflectionField(AnimationController) \
{ \
	PropertyField \
	({ \
		meta_property(name) \
		meta_property(m_curStateName) \
		meta_property(m_curState) \
		meta_property(StateVec) \
		meta_property(m_anyStateVec) \
		meta_property(m_avatarMask) \
	}); \
	FieldEnd(AnimationController, PropertyOnly) \
};
