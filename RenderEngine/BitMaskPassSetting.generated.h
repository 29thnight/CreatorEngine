#pragma once

#define ReflectBitMaskPassSetting \
ReflectionField(BitMaskPassSetting) \
{ \
	PropertyField \
	({ \
		meta_property(isOn) \
		meta_property(blurOutline) \
		meta_property(outlineVelocity) \
		meta_property(m_color1) \
		meta_property(m_color2) \
		meta_property(m_color3) \
		meta_property(m_color4) \
		meta_property(m_color5) \
		meta_property(m_color6) \
		meta_property(m_color7) \
		meta_property(m_color8) \
	}); \
	FieldEnd(BitMaskPassSetting, PropertyOnly) \
};
