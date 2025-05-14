#pragma once

#define ReflectSpriteRenderer \
ReflectionFieldInheritance(SpriteRenderer, Component) \
{ \
	PropertyField \
	({ \
		meta_property(m_Sprite) \
	}); \
	FieldEnd(SpriteRenderer, PropertyOnlyInheritance) \
};
