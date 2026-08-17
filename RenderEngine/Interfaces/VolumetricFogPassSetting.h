#pragma once
#include "Reflection.hpp" // CT3: was transitive via Core.Minimal.h
#include "Core.Minimal.h"
struct VolumetricFogPassSetting
{
   public:
   static consteval auto reflect()
   {
       using Self = VolumetricFogPassSetting;
       return meta::schema<Self>(
           meta::field<&Self::mAnisotropy>,
           meta::field<&Self::mDensity>,
           meta::field<&Self::mStrength>,
           meta::field<&Self::mThicknessFactor>,
           meta::field<&Self::mBlendingWithSceneColorFactor>,
           meta::field<&Self::mPreviousFrameBlendFactor>,
           meta::field<&Self::mCustomNearPlane>,
           meta::field<&Self::mCustomFarPlane>,
           meta::field<&Self::isOn>);
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
