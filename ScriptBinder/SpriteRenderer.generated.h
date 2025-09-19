#pragma once

#define ReflectSpriteRenderer \
ReflectionFieldInheritance(SpriteRenderer, Component) \
{ \
	PropertyField \
	({ \
		meta_property(m_SpritePath) \
		meta_property(m_CustomPSOName) \
		meta_property(m_orderInLayer) \
		meta_property(m_billboardType) \
		meta_property(m_billboardAxis) \
		meta_property(m_enableDepth) \
	}); \
	FieldEnd(SpriteRenderer, PropertyOnlyInheritance) \
};
