#pragma once

#define ReflectTransform \
ReflectionField(Transform) \
{ \
	PropertyField \
	({ \
		meta_property(position) \
		meta_property(rotation) \
		meta_property(scale) \
	}); \
	FieldEnd(Transform, PropertyOnly) \
};
