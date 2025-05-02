#pragma once

#define ReflectaniParameter \
ReflectionField(AnimationParameter) \
{ \
	PropertyField \
	({ \
		meta_property(vType) \
		meta_property(name) \
		meta_property(fValue) \
		meta_property(iValue) \
		meta_property(bValue) \
	}); \
	FieldEnd(AnimationParameter, PropertyOnly) \
};
