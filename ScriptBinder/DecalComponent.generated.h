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
		meta_property(sliceX) \
		meta_property(sliceY) \
		meta_property(sliceNumber) \
		meta_property(useAnimation) \
		meta_property(slicePerSeconds) \
		meta_property(isLoop) \
	}); \
	FieldEnd(DecalComponent, PropertyOnlyInheritance) \
};
