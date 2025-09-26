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
		meta_property(volumetricFog) \
		meta_property(skyboxTextureName) \
		meta_property(m_isSkyboxEnabled) \
		meta_property(m_windDirection) \
		meta_property(m_windStrength) \
		meta_property(m_windSpeed) \
		meta_property(m_windWaveFrequency) \
	}); \
	FieldEnd(RenderPassSettings, PropertyOnly) \
};
