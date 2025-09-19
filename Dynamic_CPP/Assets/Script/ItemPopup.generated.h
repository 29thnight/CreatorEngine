#pragma once

#define ReflectItemPopup \
ReflectionScriptField(ItemPopup) \
{ \
	PropertyField \
	({ \
		meta_property(m_startPos) \
		meta_property(m_targetOffset) \
		meta_property(m_targetScale) \
		meta_property(m_active) \
		meta_property(m_duration) \
		meta_property(m_bobTime) \
		meta_property(m_bobAmp0) \
		meta_property(m_bobFreq) \
		meta_property(m_bobPhase) \
		meta_property(m_bobDamping) \
	}); \
	FieldEnd(ItemPopup, PropertyOnly) \
};
