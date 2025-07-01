#pragma once

#define ReflectEffectComponent \
ReflectionFieldInheritance(EffectComponent, Component) \
{ \
	PropertyField \
	({ \
		meta_property(num) \
	}); \
	FieldEnd(EffectComponent, PropertyOnlyInheritance) \
};
