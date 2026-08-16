#pragma once

#define ReflectPrefabOverride \
ReflectionField(PrefabOverride) \
{ \
	PropertyField \
	({ \
		meta_property(m_componentType) \
		meta_property(m_propertyName) \
		meta_property(m_valueYaml) \
	}); \
	FieldEnd(PrefabOverride, PropertyOnly) \
};
