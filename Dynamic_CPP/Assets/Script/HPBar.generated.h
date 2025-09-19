#pragma once

#define ReflectHPBar \
ReflectionScriptField(HPBar) \
{ \
	PropertyField \
	({ \
		meta_property(targetIndex) \
		meta_property(screenOffset) \
	}); \
	FieldEnd(HPBar, PropertyOnly) \
};
