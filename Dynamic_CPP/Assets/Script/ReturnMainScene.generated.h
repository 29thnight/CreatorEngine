#pragma once

#define ReflectReturnMainScene \
ReflectionScriptFieldInheritance(ReturnMainScene, ImageButton) \
{ \
	PropertyField \
	({ \
		meta_property(m_isEntering) \
	}); \
	FieldEnd(ReturnMainScene, PropertyOnlyScriptInheritance) \
};
