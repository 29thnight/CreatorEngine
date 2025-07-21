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
		meta_property(m_useAbsolutePosition) \
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
		meta_method(SetLoop, "loop") \
		meta_method(SetDuration, "duration") \
		meta_method(SetTimeScale, "timeScale") \
		meta_method(ForceFinishEffect) \
	}); \
	FieldEnd(EffectComponent, PropertyAndMethodInheritance) \
};
