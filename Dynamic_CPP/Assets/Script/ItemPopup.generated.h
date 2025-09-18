#pragma once

#define ReflectItemPopup \
ReflectionScriptField(ItemPopup) \
{ \
	PropertyField \
	({ \
		meta_property(m_active) \
	}); \
	FieldEnd(ItemPopup, PropertyOnly) \
};
