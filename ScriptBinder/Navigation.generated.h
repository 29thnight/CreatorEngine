#pragma once

#define ReflectNavigation \
ReflectionField(Navigation) \
{ \
	PropertyField \
	({ \
		meta_property(mode) \
		meta_property(navObject) \
	}); \
	FieldEnd(Navigation, PropertyOnly) \
};
