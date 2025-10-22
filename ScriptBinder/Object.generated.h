#pragma once

#define ReflectObject \
ReflectionField(Object) \
{ \
	PropertyField \
	({ \
		meta_property(m_name) \
		meta_property(m_instanceID) \
		meta_property(m_isEnabled) \
	}); \
	FieldEnd(Object, PropertyOnly) \
};
