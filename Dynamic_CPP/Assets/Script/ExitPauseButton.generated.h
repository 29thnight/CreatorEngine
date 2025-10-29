#pragma once

#define ReflectExitPauseButton \
ReflectionScriptFieldInheritance(ExitPauseButton, ImageButton) \
{ \
	PropertyField \
	({ \
		meta_property(m_isEntering) \
	}); \
	FieldEnd(ExitPauseButton, PropertyOnlyScriptInheritance) \
};
