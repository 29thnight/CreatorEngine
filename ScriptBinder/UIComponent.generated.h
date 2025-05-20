#pragma once

#define ReflectUIComponent \
ReflectionFieldInheritance(UIComponent, Component) \
{ \
	PropertyField \
	({ \
		meta_property(_layerorder) \
	}); \
	FieldEnd(UIComponent, PropertyOnlyInheritance) \
};
