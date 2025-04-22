#pragma once

#define ReflectUIComponent \
ReflectionFieldInheritance(UIComponent, Component) \
{ \
	PropertyField \
	({ \
		meta_property(_layerorder) \
		meta_property(m_IsEnabled) \
	}); \
	FieldEnd(UIComponent, PropertyOnlyInheritance) \
};
