#pragma once

#define ReflectSpriteSheetComponent \
ReflectionFieldInheritance(SpriteSheetComponent, UIComponent) \
{ \
	PropertyField \
	({ \
		meta_property(m_spriteSheetPath) \
	}); \
	FieldEnd(SpriteSheetComponent, PropertyOnlyInheritance) \
};
