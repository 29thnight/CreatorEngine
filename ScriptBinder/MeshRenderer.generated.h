#pragma once

#define ReflectMeshRenderer \
ReflectionFieldInheritance(MeshRenderer, Component) \
{ \
	PropertyField \
	({ \
		meta_property(m_Material) \
		meta_property(m_Mesh) \
		meta_property(m_LightMapping) \
		meta_property(m_bitflag) \
		meta_property(m_isSkinnedMesh) \
		meta_property(m_shadowRecive) \
		meta_property(m_shadowCast) \
		meta_property(m_isEnableLOD) \
	}); \
	FieldEnd(MeshRenderer, PropertyOnlyInheritance) \
};
