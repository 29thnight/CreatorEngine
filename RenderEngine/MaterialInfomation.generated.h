#pragma once

#define ReflectMaterialInfomation \
ReflectionField(MaterialInfomation) \
{ \
	PropertyField \
	({ \
		meta_property(m_baseColor) \
		meta_property(m_metallic) \
		meta_property(m_roughness) \
	}); \
	FieldEnd(MaterialInfomation, PropertyOnly) \
};
