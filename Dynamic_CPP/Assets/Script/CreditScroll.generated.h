#pragma once

#define ReflectCreditScroll \
ReflectionScriptField(CreditScroll) \
{ \
	PropertyField \
	({ \
		meta_property(m_scrollSpeed) \
		meta_property(m_fastMultiplier) \
		meta_property(m_startY) \
		meta_property(m_endY) \
		meta_property(m_fadeInDuration) \
		meta_property(m_triggerFadeOnce) \
	}); \
	FieldEnd(CreditScroll, PropertyOnly) \
};
