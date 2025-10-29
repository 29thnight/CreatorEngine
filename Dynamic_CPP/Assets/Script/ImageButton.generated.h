#pragma once

#define ReflectImageButton \
ReflectionScriptField(ImageButton) \
{ \
	PropertyField \
	({ \
		meta_property(m_isTintChange) \
	}); \
	FieldEnd(ImageButton, PropertyOnly) \
};
