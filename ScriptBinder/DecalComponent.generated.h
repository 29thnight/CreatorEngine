#pragma once

#define ReflectDecalComponent \
ReflectionFieldInheritance(DecalComponent, Component) \
{ \
	PropertyField \
	({ \
		meta_property(m_diffusefileName) \
		meta_property(m_normalFileName) \
		meta_property(m_ormFileName) \
		meta_property(m_decalTexture) \
		meta_property(m_normalTexture) \
		meta_property(m_occluroughmetalTexture) \
	}); \
	FieldEnd(DecalComponent, PropertyOnlyInheritance) \
};
