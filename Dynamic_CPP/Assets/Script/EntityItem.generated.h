#pragma once

#define ReflectEntityItem \
ReflectionScriptField(EntityItem) \
{ \
	PropertyField \
	({ \
		meta_property(indicatorDistacne) \
		meta_property(itemCode) \
	}); \
	FieldEnd(EntityItem, PropertyOnly) \
};
