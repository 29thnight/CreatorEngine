#pragma once

#define ReflectItemUIPopup \
ReflectionScriptField(ItemUIPopup) \
{ \
	PropertyField \
	({ \
		meta_property(centerUV) \
		meta_property(radiusUV) \
		meta_property(percent) \
		meta_property(startAngle) \
		meta_property(clockwise) \
		meta_property(featherAngle) \
		meta_property(tint) \
		meta_property(itemID) \
		meta_property(rarityID) \
		meta_property(m_baseSize) \
		meta_property(m_popupSize) \
		meta_property(m_targetSize) \
		meta_property(m_duration) \
		meta_property(m_requiredSelectHold) \
	}); \
	MethodField \
	({ \
		meta_method(PurshaseButton) \
		meta_method(ReleaseKey) \
	}); \
	FieldEnd(ItemUIPopup, PropertyAndMethod) \
};
