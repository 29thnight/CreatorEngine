#pragma once

#define ReflectAvatarMask \
ReflectionField(AvatarMask) \
{ \
	PropertyField \
	({ \
		meta_property(isupper) \
	}); \
	FieldEnd(AvatarMask, PropertyOnly) \
};
