#pragma once

#define ReflectMeshRenderer \
ReflectionFieldInheritance(MeshRenderer, Component) \
{ \
	PropertyField \
	({ \
		meta_property(m_Material) \
		meta_property(m_Mesh) \
		meta_property(m_LightMapping) \
	}); \
	FieldEnd(MeshRenderer, PropertyOnlyInheritance) \
};
