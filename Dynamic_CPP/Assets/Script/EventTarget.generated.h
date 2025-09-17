#pragma once

#define ReflectEventTarget \
ReflectionScriptField(EventTarget) \
{ \
	PropertyField \
	({ \
		meta_property(m_eventId) \
		meta_property(m_varName) \
		meta_property(m_value) \
	}); \
	MethodField \
	({ \
		meta_method(Apply) \
	}); \
	FieldEnd(EventTarget, PropertyAndMethod) \
};
