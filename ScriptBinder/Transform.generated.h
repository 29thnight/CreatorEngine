#pragma once

#define ReflectTransform \
ReflectionField(Transform) \
{ \
	PropertyField \
	({ \
		meta_property(position) \
		meta_property(rotation) \
		meta_property(scale) \
		meta_property(m_parentID) \
		meta_property(m_dirty) \
	}); \
	FieldEnd(Transform, PropertyOnly) \
};
