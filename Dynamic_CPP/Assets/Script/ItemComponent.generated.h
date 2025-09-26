#pragma once

#define ReflectItemComponent \
ReflectionScriptField(ItemComponent) \
{ \
	PropertyField \
	({ \
		meta_property(m_playerID) \
		meta_property(m_itemID) \
		meta_property(m_itemRarity) \
	}); \
	FieldEnd(ItemComponent, PropertyOnly) \
};
