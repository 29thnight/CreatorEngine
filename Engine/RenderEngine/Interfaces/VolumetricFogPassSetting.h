#pragma once
#include "Reflection.hpp" // CT3: was transitive via Core.Minimal.h
#include "Core.Minimal.h"
struct [[reflgen::reflect]] VolumetricFogPassSetting
{
   public:
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
