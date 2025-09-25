#pragma once

#define ReflectSelectTimer \
ReflectionScriptField(SelectTimer) \
{ \
	PropertyField \
	({ \
		meta_property(m_remainTimeSetting) \
	}); \
	FieldEnd(SelectTimer, PropertyOnly) \
};
