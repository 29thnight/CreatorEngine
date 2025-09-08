#pragma once

#define ReflectCanvas \
ReflectionFieldInheritance(Canvas, Component) \
{ \
	PropertyField \
	({ \
		meta_property(CanvasOrder) \
		meta_property(CanvasName) \
	}); \
	FieldEnd(Canvas, PropertyOnlyInheritance) \
};
