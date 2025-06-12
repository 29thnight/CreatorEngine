#pragma once

#define ReflectAvatarMask \
ReflectionField(AvatarMask) \
{ \
	PropertyField \
	({ \
		meta_property(m_BoneMasks) \
		meta_property(isHumanoid) \
		meta_property(useAll) \
		meta_property(useUpper) \
		meta_property(useLower) \
	}); \
	FieldEnd(AvatarMask, PropertyOnly) \
};
