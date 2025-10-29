#pragma once

#define ReflectCreditsButton \
ReflectionScriptFieldInheritance(CreditsButton, ImageButton) \
{ \
	PropertyField \
	({ \
		meta_property(m_isEntering) \
	}); \
	FieldEnd(CreditsButton, PropertyOnlyScriptInheritance) \
};
