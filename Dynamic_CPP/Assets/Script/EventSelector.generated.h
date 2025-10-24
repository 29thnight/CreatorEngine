#pragma once

#define ReflectEventSelector \
ReflectionScriptField(EventSelector) \
{ \
	PropertyField \
	({ \
		meta_property(m_defaultMessage) \
		meta_property(m_slideInDuration) \
		meta_property(m_slideOutDuration) \
		meta_property(m_hiddenOffset) \
	}); \
	FieldEnd(EventSelector, PropertyOnly) \
};
