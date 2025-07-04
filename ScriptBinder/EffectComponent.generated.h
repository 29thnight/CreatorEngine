#pragma once

#define ReflectEffectComponent \
ReflectionFieldInheritance(EffectComponent, Component) \
{ \
	PropertyField \
	({ \
		meta_property(m_effectTemplateName) \
		meta_property(m_isPlaying) \
		meta_property(m_isPaused) \
		meta_property(m_timeScale) \
		meta_property(m_loop) \
		meta_property(m_duration) \
		meta_property(m_currentTime) \
		meta_property(num) \
		meta_property(m_particleEvents) \
	}); \
	MethodField \
	({ \
		meta_method(Apply) \
		meta_method(PlayPreview) \
		meta_method(StopEffect) \
		meta_method(PauseEffect) \
		meta_method(ResumeEffect) \
		meta_method(ChangeEffect, "std", "newEffectName") \
		meta_method(PlayEffectByName, "std", "effectName") \
		meta_method(AddParticleEvent, "std", "eventName") \
	}); \
	FieldEnd(EffectComponent, PropertyAndMethodInheritance) \
};
