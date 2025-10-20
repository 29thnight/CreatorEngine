#pragma once

#define ReflectSettingWindowUI \
ReflectionScriptField(SettingWindowUI) \
{ \
	PropertyField \
	({ \
		meta_property(m_slideSpeed) \
	}); \
	FieldEnd(SettingWindowUI, PropertyOnly) \
};
