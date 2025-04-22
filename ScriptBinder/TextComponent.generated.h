#pragma once

#define ReflectTextComponent \
ReflectionFieldInheritance(TextComponent, UIComponent) \
{ \
	PropertyField \
	({ \
		meta_property(message) \
		meta_property(relpos) \
	}); \
	FieldEnd(TextComponent, PropertyOnlyInheritance) \
};
