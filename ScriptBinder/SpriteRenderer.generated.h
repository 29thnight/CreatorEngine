#pragma once

#define ReflectSpriteRenderer \
ReflectionFieldInheritance(SpriteRenderer, Component) \
{ \
	PropertyField \
	({ \
		meta_property(m_SpritePath) \
		meta_property(m_CustomPSOName) \
		meta_property(m_orderInLayer) \
	}); \
	FieldEnd(SpriteRenderer, PropertyOnlyInheritance) \
};
