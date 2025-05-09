#pragma once

#define ReflectAvatarMask \
ReflectionField(AvatarMask) \
{ \
	PropertyField \
	({ \
		meta_property(isUpper) \
		meta_property(isLower) \
	}); \
	FieldEnd(AvatarMask, PropertyOnly) \
};
