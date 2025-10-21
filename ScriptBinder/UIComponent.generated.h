#pragma once

#define ReflectUIComponent \
ReflectionFieldInheritance(UIComponent, Component) \
{ \
	PropertyField \
	({ \
		meta_property(_layerorder) \
		meta_property(uiEffects) \
		meta_property(m_ownerCanvasName) \
		meta_property(navigations) \
		meta_property(m_customPixelShaderPath) \
	}); \
	FieldEnd(UIComponent, PropertyOnlyInheritance) \
};
