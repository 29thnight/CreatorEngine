#pragma once

#define ReflectAnimationController \
ReflectionField(AnimationController) \
{ \
	PropertyField \
	({ \
		meta_property(name) \
		meta_property(m_curState) \
		meta_property(StateVec) \
		meta_property(m_nodeEditor) \
		meta_property(m_anyState) \
		meta_property(useController) \
		meta_property(m_avatarMask) \
		meta_property(useMask) \
	}); \
	FieldEnd(AnimationController, PropertyOnly) \
};
