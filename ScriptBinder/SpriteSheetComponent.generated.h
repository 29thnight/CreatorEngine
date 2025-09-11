#pragma once

#define ReflectSpriteSheetComponent \
ReflectionFieldInheritance(SpriteSheetComponent, UIComponent) \
{ \
	PropertyField \
	({ \
		meta_property(m_spriteSheetPath) \
		meta_property(m_frameDuration) \
		meta_property(m_isLoop) \
		meta_property(m_isPreview) \
	}); \
	FieldEnd(SpriteSheetComponent, PropertyOnlyInheritance) \
};
