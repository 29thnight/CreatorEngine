#pragma once

#define ReflectUIGageTest \
ReflectionScriptField(UIGageTest) \
{ \
	PropertyField \
	({ \
		meta_property(centerUV) \
		meta_property(radiusUV) \
		meta_property(percent) \
		meta_property(startAngle) \
		meta_property(clockwise) \
		meta_property(featherAngle) \
		meta_property(tint) \
	}); \
	FieldEnd(UIGageTest, PropertyOnly) \
};
