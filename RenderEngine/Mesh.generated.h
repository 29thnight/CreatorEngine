#pragma once

#define ReflectMesh \
ReflectionField(Mesh) \
{ \
	PropertyField \
	({ \
		meta_property(m_name) \
		meta_property(m_materialIndex) \
		meta_property(m_LODThresholds) \
	}); \
	FieldEnd(Mesh, PropertyOnly) \
};
