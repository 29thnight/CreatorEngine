#pragma once
#include "Reflection.hpp" // CT3: was transitive via Core.Minimal.h
#include "Core.Minimal.h"
struct BitMaskPassSetting
{
   static consteval auto describe()
   {
       return meta::describe<BitMaskPassSetting>(
           meta::member<&BitMaskPassSetting::isOn>(),
           meta::member<&BitMaskPassSetting::blurOutline>(),
           meta::member<&BitMaskPassSetting::outlineVelocity>(),
           meta::member<&BitMaskPassSetting::m_color1>(),
           meta::member<&BitMaskPassSetting::m_color2>(),
           meta::member<&BitMaskPassSetting::m_color3>(),
           meta::member<&BitMaskPassSetting::m_color4>(),
           meta::member<&BitMaskPassSetting::m_color5>(),
           meta::member<&BitMaskPassSetting::m_color6>(),
           meta::member<&BitMaskPassSetting::m_color7>(),
           meta::member<&BitMaskPassSetting::m_color8>());
   }
	BitMaskPassSetting() = default;

	bool isOn = true;
	bool blurOutline = false;
	float outlineVelocity = 3.f;
	Mathf::Color4 m_color1 = { 1.f, 0.f, 0.f, 3.f }; // red
	Mathf::Color4 m_color2 = { 0.f, 1.f, 0.f, 3.f }; // Green
	Mathf::Color4 m_color3 = { 0.f, 0.f, 1.f, 3.f }; // Blue
	Mathf::Color4 m_color4 = { 1.f, 1.f, 0.f, 3.f }; // Yellow
	Mathf::Color4 m_color5 = { 1.f, 0.f, 1.f, 3.f }; // Magenta
	Mathf::Color4 m_color6 = { 0.f, 1.f, 1.f, 3.f }; // Cyan
	Mathf::Color4 m_color7 = { 0.5f, 0.5f, 0.5f, 3.f }; // Gray
	Mathf::Color4 m_color8 = { 0.2f, 0.2f, 0.2f, 3.f }; // Dark Gray}
};
