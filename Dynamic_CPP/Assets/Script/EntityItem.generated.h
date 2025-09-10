#pragma once

#define ReflectEntityItem \
ReflectionScriptField(EntityItem) \
{ \
	PropertyField \
	({ \
		meta_property(asisTail) \
		meta_property(m_rigid) \
		meta_property(indicatorDistacne) \
		meta_property(itemCode) \
		meta_property(itemType) \
	}); \
	FieldEnd(EntityItem, PropertyOnly) \
};
