#pragma once

#define ReflectMaterial \
ReflectionField(Material) \
{ \
	PropertyField \
	({ \
		meta_property(m_name) \
		meta_property(m_baseColorTexName) \
		meta_property(m_normalTexName) \
		meta_property(m_ORM_TexName) \
		meta_property(m_AO_TexName) \
		meta_property(m_EmissiveTexName) \
		meta_property(m_materialInfo) \
		meta_property(m_flowInfo) \
		meta_property(m_fileGuid) \
		meta_property(m_renderingMode) \
		meta_property(m_shaderPSOGuid) \
		meta_property(m_cbufferValues) \
	}); \
	FieldEnd(Material, PropertyOnly) \
};
