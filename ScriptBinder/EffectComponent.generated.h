#pragma once

#define ReflectEffectComponent \
ReflectionFieldInheritance(EffectComponent, Component) \
{ \
	PropertyField \
	({ \
		meta_property(num) \
	}); \
	MethodField \
	({ \
		meta_method(Apply) \
		meta_method(PlayPreview) \
	}); \
	FieldEnd(EffectComponent, PropertyAndMethodInheritance) \
};
