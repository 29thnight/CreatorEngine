#pragma once

#define ReflectComponent \
ReflectionFieldInheritance(Component, Object) \
{ \
	PropertyField \
	({ \
		meta_property(m_instanceID) \
	}); \
	FieldEnd(Component, PropertyOnlyInheritance) \
};
