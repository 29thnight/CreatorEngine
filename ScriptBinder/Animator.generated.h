#pragma once

#define ReflectAnimator \
ReflectionFieldInheritance(Animator, Component) \
{ \
	PropertyField \
	({ \
		meta_property(m_Skeleton) \
		meta_property(m_AnimIndexChosen) \
		meta_property(m_AnimIndex) \
		meta_property(m_Motion) \
		meta_property(m_animationControllers) \
		meta_property(Parameters) \
	}); \
	MethodField \
	({ \
		meta_method(UpdateAnimation) \
		meta_method(CreateController_UI) \
	}); \
	FieldEnd(Animator, PropertyAndMethodInheritance) \
};
