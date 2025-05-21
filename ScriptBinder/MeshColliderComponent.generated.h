#pragma once

#define ReflectMeshColliderComponent \
ReflectionFieldInheritance(MeshColliderComponent, Component) \
{ \
	PropertyField \
	({ \
		meta_property(m_Info) \
		meta_property(m_posOffset) \
		meta_property(m_rotOffset) \
	}); \
	FieldEnd(MeshColliderComponent, PropertyOnlyInheritance) \
};
