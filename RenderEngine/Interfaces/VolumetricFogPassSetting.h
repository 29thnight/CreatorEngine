#pragma once
#include "Reflection.hpp" // CT3: was transitive via Core.Minimal.h
#include "Core.Minimal.h"
struct VolumetricFogPassSetting
{
   static consteval auto describe()
   {
       return meta::describe<VolumetricFogPassSetting>(
           meta::member<&VolumetricFogPassSetting::mAnisotropy>(),
           meta::member<&VolumetricFogPassSetting::mDensity>(),
           meta::member<&VolumetricFogPassSetting::mStrength>(),
           meta::member<&VolumetricFogPassSetting::mThicknessFactor>(),
           meta::member<&VolumetricFogPassSetting::mBlendingWithSceneColorFactor>(),
           meta::member<&VolumetricFogPassSetting::mPreviousFrameBlendFactor>(),
           meta::member<&VolumetricFogPassSetting::mCustomNearPlane>(),
           meta::member<&VolumetricFogPassSetting::mCustomFarPlane>(),
           meta::member<&VolumetricFogPassSetting::isOn>());
   }
	VolumetricFogPassSetting() = default;

	float mAnisotropy = 0.109f;
	float mDensity = 0.101f;
	float mStrength = 2.0f;
	float mThicknessFactor = 0.01f;
	float mBlendingWithSceneColorFactor = 0.851f;
	float mPreviousFrameBlendFactor = 0.95f;

	float mCustomNearPlane = 0.5f;
	float mCustomFarPlane = 1000.0f;

	bool isOn{ true };
};
