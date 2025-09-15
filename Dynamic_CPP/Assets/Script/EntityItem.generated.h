#pragma once

#define ReflectEntityItem \
ReflectionScriptField(EntityItem) \
{ \
	PropertyField \
	({ \
		meta_property(indicatorDistacne) \
		meta_property(itemCode) \
		meta_property(itemType) \
		meta_property(itemReward) \
	}); \
	FieldEnd(EntityItem, PropertyOnly) \
};
