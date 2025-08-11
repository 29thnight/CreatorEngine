#pragma once

#define ReflectRectTransformComponent \
ReflectionFieldInheritance(RectTransformComponent, Component) \
{ \
	PropertyField \
	({ \
		meta_property(m_anchorMin) \
		meta_property(m_anchorMax) \
		meta_property(m_anchoredPosition) \
		meta_property(m_sizeDelta) \
		meta_property(m_pivot) \
		meta_property(m_worldRect) \
	}); \
	FieldEnd(RectTransformComponent, PropertyOnlyInheritance) \
};
