#pragma once

#define ReflectStartButton \
ReflectionScriptFieldInheritance(StartButton, ImageButton) \
{ \
	PropertyField \
	({ \
		meta_property(m_isClicked) \
	}); \
	FieldEnd(StartButton, PropertyOnlyScriptInheritance) \
};
