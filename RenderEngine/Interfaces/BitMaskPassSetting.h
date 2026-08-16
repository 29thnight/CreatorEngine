#pragma once
#include "Reflection.hpp" // CT3: was transitive via Core.Minimal.h
#include "Core.Minimal.h"
#include "BitMaskPassSetting.generated.h"
struct BitMaskPassSetting
{
   ReflectBitMaskPassSetting
	[[Serializable]]
	BitMaskPassSetting() = default;

	[[Property]]
	bool isOn = true;
	[[Property]]
	bool blurOutline = false;
	[[Property]]
	float outlineVelocity = 3.f;
	[[Property]]
	Mathf::Color4 m_color1 = { 1.f, 0.f, 0.f, 3.f }; // red
	[[Property]]
	Mathf::Color4 m_color2 = { 0.f, 1.f, 0.f, 3.f }; // Green
	[[Property]]
	Mathf::Color4 m_color3 = { 0.f, 0.f, 1.f, 3.f }; // Blue
	[[Property]]
	Mathf::Color4 m_color4 = { 1.f, 1.f, 0.f, 3.f }; // Yellow
	[[Property]]
	Mathf::Color4 m_color5 = { 1.f, 0.f, 1.f, 3.f }; // Magenta
	[[Property]]
	Mathf::Color4 m_color6 = { 0.f, 1.f, 1.f, 3.f }; // Cyan
	[[Property]]
	Mathf::Color4 m_color7 = { 0.5f, 0.5f, 0.5f, 3.f }; // Gray
	[[Property]]
	Mathf::Color4 m_color8 = { 0.2f, 0.2f, 0.2f, 3.f }; // Dark Gray}
};
