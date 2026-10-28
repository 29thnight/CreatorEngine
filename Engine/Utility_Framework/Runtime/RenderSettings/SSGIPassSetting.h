#pragma once
#include "../../Reflection/Reflection.hpp" // CT3: was transitive via Core.Minimal.h
#include "../../Foundation/Core.Minimal.h"

struct [[reflgen::reflect, reflgen::public_methods]] SSGIPassSetting
{
   public:
    SSGIPassSetting() = default;

    bool isOn{ true };
    bool useOnlySSGI{ false };
    int useDualFilteringStep{ 2 };
    float radius{ 4.f };
    float thickness{ 0.5f };
    float intensity{ 1.f };
    int ssratio{ 4 };
};
