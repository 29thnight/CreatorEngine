#pragma once
#include "Reflection.hpp" // CT3: was transitive via Core.Minimal.h
#include "Core.Minimal.h"

struct ShadowMapPassSetting
{
   // CT4 파일럿 — 선언이 멤버보다 앞에 오는 배치 케이스(describe()가 함수라
   // complete-class 문맥에서 멤버 포인터가 형성된다).
   static consteval auto describe()
   {
       return meta::describe<ShadowMapPassSetting>(
           meta::member<&ShadowMapPassSetting::useCascade>(),
           meta::member<&ShadowMapPassSetting::isCloudOn>(),
           meta::member<&ShadowMapPassSetting::cloudSize>(),
           meta::member<&ShadowMapPassSetting::cloudDirection>(),
           meta::member<&ShadowMapPassSetting::cloudMoveSpeed>(),
           meta::member<&ShadowMapPassSetting::cloudAlpha>(),
           meta::member<&ShadowMapPassSetting::epsilon>());
   }

    ShadowMapPassSetting() = default;

    bool useCascade{ true };
    bool isCloudOn{ true };
    Mathf::Vector2 cloudSize{ 7.f, 7.f };
    Mathf::Vector2 cloudDirection{ 1.f, 1.f };
    float cloudMoveSpeed{ 0.00006f };
	float cloudAlpha{ 1.f };
    float epsilon{ 0.001f };
};
