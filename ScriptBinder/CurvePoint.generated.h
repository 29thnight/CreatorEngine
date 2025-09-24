#pragma once

#define ReflectCurvePoint \
ReflectionField(CurvePoint) \
{ \
	PropertyField \
	({ \
		meta_property(distance) \
		meta_property(gain) \
	}); \
	FieldEnd(CurvePoint, PropertyOnly) \
};
