#pragma once

#define ReflectTextComponent \
ReflectionFieldInheritance(TextComponent, UIComponent) \
{ \
	PropertyField \
	({ \
		meta_property(_isTable) \
		meta_property(relpos) \
	}); \
	FieldEnd(TextComponent, PropertyOnlyInheritance) \
};
