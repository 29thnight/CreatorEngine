#pragma once

#define ReflectAvatarMask \
ReflectionField(AvatarMask) \
{ \
	PropertyField \
	({ \
		meta_property(useAll) \
		meta_property(useUpper) \
		meta_property(useLower) \
	}); \
	FieldEnd(AvatarMask, PropertyOnly) \
};
