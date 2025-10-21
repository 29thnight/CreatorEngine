#pragma once

#define ReflectSceneTransitionUI \
ReflectionScriptField(SceneTransitionUI) \
{ \
	PropertyField \
	({ \
		meta_property(m_defaultFadeIn) \
		meta_property(m_defaultFadeOut) \
		meta_property(m_ignoreTimeScale) \
	}); \
	FieldEnd(SceneTransitionUI, PropertyOnly) \
};
