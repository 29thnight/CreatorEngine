#pragma once

#define ReflectTextComponent \
ReflectionFieldInheritance(TextComponent, UIComponent) \
{ \
	PropertyField \
	({ \
		meta_property(fontPath) \
		meta_property(message) \
		meta_property(relpos) \
		meta_property(color) \
		meta_property(manualRect) \
		meta_property(fontSize) \
		meta_property(useManualRect) \
	}); \
	FieldEnd(TextComponent, PropertyOnlyInheritance) \
};
