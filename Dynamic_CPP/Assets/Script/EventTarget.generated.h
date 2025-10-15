#pragma once

#define ReflectEventTarget \
ReflectionScriptField(EventTarget) \
{ \
	PropertyField \
	({ \
		meta_property(m_eventId) \
		meta_property(m_runtimeTag) \
	}); \
	MethodField \
	({ \
		meta_method(Apply) \
	}); \
	FieldEnd(EventTarget, PropertyAndMethod) \
};
