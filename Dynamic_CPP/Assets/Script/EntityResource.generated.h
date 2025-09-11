#pragma once

#define ReflectEntityResource \
ReflectionScriptField(EntityResource) \
{ \
	PropertyField \
	({ \
		meta_property(itemCode) \
		meta_property(itemType) \
	}); \
	FieldEnd(EntityResource, PropertyOnly) \
};
