#pragma once

#define ReflectCanvas \
ReflectionFieldInheritance(Canvas, Component) \
{ \
	PropertyField \
	({ \
		meta_property(ScaleMode) \
		meta_property(ReferenceResolution) \
		meta_property(MatchWidthOrHeight) \
		meta_property(ScaleFactor) \
		meta_property(CanvasOrder) \
		meta_property(CanvasName) \
	}); \
	FieldEnd(Canvas, PropertyOnlyInheritance) \
};
