#pragma once

#define ReflectBoneMask \
ReflectionField(BoneMask) \
{ \
	PropertyField \
	({ \
		meta_property(isEnabled) \
	}); \
	FieldEnd(BoneMask, PropertyOnly) \
};
