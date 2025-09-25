#pragma once

#define ReflectItemManager \
ReflectionScriptField(ItemManager) \
{ \
	PropertyField \
	({ \
		meta_property(CommonItemColor) \
		meta_property(RareItemColor) \
		meta_property(EpicItemColor) \
	}); \
	FieldEnd(ItemManager, PropertyOnly) \
};
