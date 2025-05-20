#pragma once

#define ReflectCanvas \
ReflectionFieldInheritance(Canvas, Component) \
{ \
	PropertyField \
	({ \
		meta_property(CanvasOrder) \
	}); \
	FieldEnd(Canvas, PropertyOnlyInheritance) \
};
