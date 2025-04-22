#pragma once

#define ReflectUIButton \
ReflectionFieldInheritance(UIButton, UIComponent) \
{ \
	MethodField \
	({ \
		meta_method(Click) \
	}); \
	FieldEnd(UIButton, MethodOnlyInheritance) \
};
