#pragma once

#define ReflectPrefab \
ReflectionFieldInheritance(Prefab, Object) \
{ \
	PropertyField \
	({ \
		meta_property(m_fileGuid) \
	}); \
	FieldEnd(Prefab, PropertyOnlyInheritance) \
};
