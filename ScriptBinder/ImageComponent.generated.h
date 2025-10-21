#pragma once

#define ReflectImageComponent \
ReflectionFieldInheritance(ImageComponent, UIComponent) \
{ \
	PropertyField \
	({ \
		meta_property(texturePaths) \
		meta_property(color) \
		meta_property(curindex) \
		meta_property(rotate) \
		meta_property(origin) \
		meta_property(unionScale) \
		meta_property(clipPercent) \
		meta_property(clipDirection) \
	}); \
	MethodField \
	({ \
		meta_method(UpdateTexture) \
	}); \
	FieldEnd(ImageComponent, PropertyAndMethodInheritance) \
};
