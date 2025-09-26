#pragma once

#define ReflectItemUIIcon \
ReflectionScriptField(ItemUIIcon) \
{ \
	PropertyField \
	({ \
		meta_property(m_playerID) \
		meta_property(m_isSetPopup) \
		meta_property(itemID) \
		meta_property(rarityID) \
		meta_property(screenOffset) \
		meta_property(popupOffset) \
		meta_property(m_duration) \
		meta_property(m_bobbing) \
		meta_property(m_bobTime) \
		meta_property(m_bobAmp0) \
		meta_property(m_bobFreq) \
		meta_property(m_bobPhase) \
		meta_property(m_bobDamping) \
	}); \
	FieldEnd(ItemUIIcon, PropertyOnly) \
};
