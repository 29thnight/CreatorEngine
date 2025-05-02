#pragma once

#define ReflectaniState \
ReflectionField(aniState) \
{ \
	PropertyField \
	({ \
		meta_property(Name) \
		meta_property(Transitions) \
		meta_property(index) \
	}); \
	FieldEnd(aniState, PropertyOnly) \
};
