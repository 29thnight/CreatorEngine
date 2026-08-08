#pragma once
#include "Core.Minimal.h"
#include "VolumetricFogPassSetting.generated.h"
struct VolumetricFogPassSetting
{
   ReflectVolumetricFogPassSetting
	[[Serializable]]
	VolumetricFogPassSetting() = default;

	[[Property]]
	float mAnisotropy = 0.109f;
	[[Property]]
	float mDensity = 0.101f;
	[[Property]]
	float mStrength = 2.0f;
	[[Property]]
	float mThicknessFactor = 0.01f;
	[[Property]]
	float mBlendingWithSceneColorFactor = 0.851f;
	[[Property]]
	float mPreviousFrameBlendFactor = 0.95f;

	[[Property]]
	float mCustomNearPlane = 0.5f;
	[[Property]]
	float mCustomFarPlane = 1000.0f;

	[[Property]]
	bool isOn{ true };
};
