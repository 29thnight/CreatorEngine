#pragma once

#define ReflectActionMap \
ReflectionField(ActionMap) \
{ \
	PropertyField \
	({ \
		meta_property(m_name) \
		meta_property(m_actions) \
	}); \
	FieldEnd(ActionMap, PropertyOnly) \
};
