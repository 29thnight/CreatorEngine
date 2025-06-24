#pragma once

#define ReflectMaterialInfomation \
ReflectionField(MaterialInfomation) \
{ \
	PropertyField \
	({ \
		meta_property(m_baseColor) \
		meta_property(m_metallic) \
		meta_property(m_roughness) \
		meta_property(m_bitflag) \
	}); \
	FieldEnd(MaterialInfomation, PropertyOnly) \
};
