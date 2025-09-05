#pragma once

#define ReflectTextComponent \
ReflectionFieldInheritance(TextComponent, UIComponent) \
{ \
	PropertyField \
	({ \
		meta_property(message) \
		meta_property(relpos) \
		meta_property(color) \
		meta_property(fontSize) \
	}); \
	FieldEnd(TextComponent, PropertyOnlyInheritance) \
};
