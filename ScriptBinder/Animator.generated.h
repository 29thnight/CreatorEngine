#pragma once

#define ReflectAnimator \
ReflectionFieldInheritance(Animator, Component) \
{ \
	PropertyField \
	({ \
		meta_property(m_Skeleton) \
		meta_property(m_TimeElapsed) \
		meta_property(m_AnimIndexChosen) \
		meta_property(curAniName) \
		meta_property(m_isLoof) \
		meta_property(m_AnimIndex) \
		meta_property(m_nextTimeElapsed) \
		meta_property(m_Motion) \
		meta_property(m_animationController) \
	}); \
	MethodField \
	({ \
		meta_method(UpdateAnimation) \
	}); \
	FieldEnd(Animator, PropertyAndMethodInheritance) \
};
