#pragma once

#define ReflectEntityItemHeal \
ReflectionScriptField(EntityItemHeal) \
{ \
	PropertyField \
	({ \
		meta_property(itemCode) \
		meta_property(itemType) \
		meta_property(healAmount) \
	}); \
	FieldEnd(EntityItemHeal, PropertyOnly) \
};
