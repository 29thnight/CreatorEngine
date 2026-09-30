#pragma once
#include <mathematics/vector2.hpp>
#include "Reflection.hpp" // CT3: was transitive via Core.Minimal.h
#include "Core.Minimal.h"

struct [[reflgen::reflect]] ShadowMapPassSetting
{
   public:

    ShadowMapPassSetting() = default;

    bool useCascade{ true };
    bool isCloudOn{ true };
    math::vector2 cloudSize{ 7.f, 7.f };
    math::vector2 cloudDirection{ 1.f, 1.f };
    float cloudMoveSpeed{ 0.00006f };
	float cloudAlpha{ 1.f };
    float epsilon{ 0.001f };
};
