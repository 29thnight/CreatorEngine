#pragma once

#define ReflectSpriteRenderer \
ReflectionFieldInheritance(SpriteRenderer, Component) \
{ \
	PropertyField \
	({ \
		meta_property(m_SpritePath) \
		meta_property(m_VertexShaderName) \
		meta_property(m_PixelShaderName) \
		meta_property(m_orderInLayer) \
	}); \
	FieldEnd(SpriteRenderer, PropertyOnlyInheritance) \
};
