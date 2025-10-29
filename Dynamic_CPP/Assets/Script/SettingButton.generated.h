#pragma once

#define ReflectSettingButton \
ReflectionScriptFieldInheritance(SettingButton, ImageButton) \
{ \
	PropertyField \
	({ \
		meta_property(m_isEntering) \
	}); \
	FieldEnd(SettingButton, PropertyOnlyScriptInheritance) \
};
