#pragma once

#define ReflectMaterial \
ReflectionField(Material) \
{ \
	PropertyField \
	({ \
		meta_property(m_name) \
		meta_property(m_materialInfo) \
		meta_property(m_flowInfo) \
		meta_property(m_fileGuid) \
		meta_property(m_renderingMode) \
	}); \
	FieldEnd(Material, PropertyOnly) \
};
