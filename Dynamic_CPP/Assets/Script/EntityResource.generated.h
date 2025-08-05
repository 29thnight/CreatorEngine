#pragma once

#define ReflectEntityResource \
ReflectionScriptField(EntityResource) \
{ \
	PropertyField \
	({ \
		meta_property(itemCode) \
	}); \
	FieldEnd(EntityResource, PropertyOnly) \
};
