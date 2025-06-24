#pragma once

#define ReflectInputAction \
ReflectionField(InputAction) \
{ \
	PropertyField \
	({ \
		meta_property(funName) \
	}); \
	FieldEnd(InputAction, PropertyOnly) \
};
