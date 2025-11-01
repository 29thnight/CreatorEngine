#pragma once

#define ReflectHPBar \
ReflectionScriptField(HPBar) \
{ \
	PropertyField \
	({ \
		meta_property(targetIndex) \
		meta_property(screenOffset) \
		meta_property(m_warningPersent) \
		meta_property(m_blinkHz) \
	}); \
	FieldEnd(HPBar, PropertyOnly) \
};
