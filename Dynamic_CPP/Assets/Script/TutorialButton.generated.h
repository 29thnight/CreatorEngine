#pragma once

#define ReflectTutorialButton \
ReflectionScriptFieldInheritance(TutorialButton, ImageButton) \
{ \
	PropertyField \
	({ \
		meta_property(m_isEntering) \
	}); \
	FieldEnd(TutorialButton, PropertyOnlyScriptInheritance) \
};
