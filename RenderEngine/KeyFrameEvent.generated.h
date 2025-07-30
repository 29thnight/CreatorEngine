#pragma once

#define ReflectKeyFrameEvent \
ReflectionField(KeyFrameEvent) \
{ \
	PropertyField \
	({ \
		meta_property(m_eventName) \
		meta_property(m_scriptName) \
		meta_property(m_funName) \
		meta_property(key) \
	}); \
	FieldEnd(KeyFrameEvent, PropertyOnly) \
};
