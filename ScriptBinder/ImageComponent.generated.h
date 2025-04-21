#pragma once

#define ReflectImageComponent \
ReflectionFieldInheritance(ImageComponent, UIComponent) \
{ \
	PropertyField \
	({ \
		meta_property(curindex) \
	}); \
	MethodField \
	({ \
		meta_method(UpdateTexture) \
	}); \
	FieldEnd(ImageComponent, PropertyAndMethodInheritance) \
};
