#pragma once

#define ReflectSpriteRenderer \
ReflectionFieldInheritance(SpriteRenderer, Component) \
{ \
	PropertyField \
	({ \
		meta_property(m_Sprite) \
		meta_property(m_IsEnabled) \
	}); \
	FieldEnd(SpriteRenderer, PropertyOnlyInheritance) \
};
