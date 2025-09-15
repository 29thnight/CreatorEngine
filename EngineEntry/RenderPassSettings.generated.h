#pragma once

#define ReflectRenderPassSettings \
ReflectionField(RenderPassSettings) \
{ \
	PropertyField \
	({ \
		meta_property(aa) \
		meta_property(ssao) \
		meta_property(shadow) \
		meta_property(deferred) \
		meta_property(bloom) \
		meta_property(ssgi) \
		meta_property(vignette) \
		meta_property(colorGrading) \
		meta_property(toneMap) \
		meta_property(skyboxTextureName) \
		meta_property(m_isSkyboxEnabled) \
	}); \
	FieldEnd(RenderPassSettings, PropertyOnly) \
};
