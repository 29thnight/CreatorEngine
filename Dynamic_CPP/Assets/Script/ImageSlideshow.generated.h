#pragma once

#define ReflectImageSlideshow \
ReflectionScriptField(ImageSlideshow) \
{ \
	PropertyField \
	({ \
		meta_property(m_interval) \
		meta_property(m_playOnStart) \
		meta_property(m_loop) \
		meta_property(m_pingPong) \
		meta_property(m_resetToFirstOnStart) \
		meta_property(m_fadeEnabled) \
		meta_property(m_fadeDuration) \
		meta_property(m_stopFadeOnStop) \
		meta_property(m_stopFadeHoldVisible) \
		meta_property(m_stopHoldDuration) \
	}); \
	MethodField \
	({ \
		meta_method(Play) \
		meta_method(Pause) \
		meta_method(Stop) \
		meta_method(Next, "step") \
		meta_method(Prev, "step") \
		meta_method(SetFrame, "index") \
	}); \
	FieldEnd(ImageSlideshow, PropertyAndMethod) \
};
