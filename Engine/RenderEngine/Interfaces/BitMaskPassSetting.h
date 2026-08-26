#pragma once
#include "Reflection.hpp" // CT3: was transitive via Core.Minimal.h
#include "Core.Minimal.h"
#include <mathematics/color.hpp>
struct BitMaskPassSetting
{
   public:
   static consteval auto reflect()
   {
       using Self = BitMaskPassSetting;
       return meta::schema<Self>(
           meta::field<&Self::isOn>,
           meta::field<&Self::blurOutline>,
           meta::field<&Self::outlineVelocity>,
           meta::field<&Self::m_color1>,
           meta::field<&Self::m_color2>,
           meta::field<&Self::m_color3>,
           meta::field<&Self::m_color4>,
           meta::field<&Self::m_color5>,
           meta::field<&Self::m_color6>,
           meta::field<&Self::m_color7>,
           meta::field<&Self::m_color8>);
   }
	BitMaskPassSetting() = default;

	bool isOn = true;
	bool blurOutline = false;
	float outlineVelocity = 3.f;
    math::color m_color1 = { 1.f, 0.f, 0.f, 3.f }; // red
    math::color m_color2 = { 0.f, 1.f, 0.f, 3.f }; // Green
    math::color m_color3 = { 0.f, 0.f, 1.f, 3.f }; // Blue
    math::color m_color4 = { 1.f, 1.f, 0.f, 3.f }; // Yellow
    math::color m_color5 = { 1.f, 0.f, 1.f, 3.f }; // Magenta
    math::color m_color6 = { 0.f, 1.f, 1.f, 3.f }; // Cyan
    math::color m_color7 = { 0.5f, 0.5f, 0.5f, 3.f }; // Gray
    math::color m_color8 = { 0.2f, 0.2f, 0.2f, 3.f }; // Dark Gray}
};
