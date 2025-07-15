#pragma once

#define ReflectBlackBoardValue \
ReflectionField(BlackBoardValue) \
{ \
	PropertyField \
	({ \
		meta_property(Type) \
		meta_property(BoolValue) \
		meta_property(IntValue) \
		meta_property(FloatValue) \
		meta_property(StringValue) \
		meta_property(Vec2Value) \
		meta_property(Vec3Value) \
		meta_property(Vec4Value) \
	}); \
	FieldEnd(BlackBoardValue, PropertyOnly) \
};
