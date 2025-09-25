#pragma once

#define ReflectItemUIPopup \
ReflectionScriptField(ItemUIPopup) \
{ \
	PropertyField \
	({ \
		meta_property(itemID) \
		meta_property(rarityID) \
		meta_property(m_baseSize) \
		meta_property(m_popupSize) \
		meta_property(m_targetSize) \
		meta_property(m_duration) \
	}); \
	FieldEnd(ItemUIPopup, PropertyOnly) \
};
