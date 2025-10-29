#pragma once

#define ReflectRestartButton \
ReflectionScriptFieldInheritance(RestartButton, ImageButton) \
{ \
	PropertyField \
	({ \
		meta_property(m_isEntering) \
	}); \
	FieldEnd(RestartButton, PropertyOnlyScriptInheritance) \
};
