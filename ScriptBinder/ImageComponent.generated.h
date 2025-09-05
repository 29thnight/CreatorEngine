#pragma once

#define ReflectImageComponent \
ReflectionFieldInheritance(ImageComponent, UIComponent) \
{ \
	PropertyField \
	({ \
		meta_property(curindex) \
		meta_property(color) \
		meta_property(rotate) \
		meta_property(origin) \
		meta_property(texturePaths) \
	}); \
	MethodField \
	({ \
		meta_method(UpdateTexture) \
	}); \
	FieldEnd(ImageComponent, PropertyAndMethodInheritance) \
};
