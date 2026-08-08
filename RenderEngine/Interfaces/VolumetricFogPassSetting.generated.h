#pragma once

#define ReflectVolumetricFogPassSetting \
ReflectionField(VolumetricFogPassSetting) \
{ \
	PropertyField \
	({ \
		meta_property(mAnisotropy) \
		meta_property(mDensity) \
		meta_property(mStrength) \
		meta_property(mThicknessFactor) \
		meta_property(mBlendingWithSceneColorFactor) \
		meta_property(mPreviousFrameBlendFactor) \
		meta_property(mCustomNearPlane) \
		meta_property(mCustomFarPlane) \
		meta_property(isOn) \
	}); \
	FieldEnd(VolumetricFogPassSetting, PropertyOnly) \
};
