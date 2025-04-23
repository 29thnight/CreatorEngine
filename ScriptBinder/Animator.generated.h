#pragma once

#define ReflectAnimator \
ReflectionFieldInheritance(Animator, Component) \
{ \
	PropertyField \
	({ \
		meta_property(m_Skeleton) \
		meta_property(m_TimeElapsed) \
		meta_property(m_AnimIndexChosen) \
		meta_property(m_AnimIndex) \
	}); \
	MethodField \
	({ \
		meta_method(UpdateAnimation) \
	}); \
	FieldEnd(Animator, PropertyAndMethodInheritance) \
};
