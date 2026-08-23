#pragma once
#include "Reflection.hpp" // CT3: was transitive via Core.Minimal.h
#include "Core.Minimal.h"

struct ShadowMapPassSetting
{
   public:
   // CT4 파일럿이었던 타입 — CT8에서 canonical 변수로 전환. 정적 멤버 초기치는
   // complete-class 문맥이 아니라 참조 멤버 선언 뒤(하단)에 온다.
   static consteval auto reflect()
   {
       using Self = ShadowMapPassSetting;
       return meta::schema<Self>(
           meta::field<&Self::useCascade>,
           meta::field<&Self::isCloudOn>,
           meta::field<&Self::cloudSize>,
           meta::field<&Self::cloudDirection>,
           meta::field<&Self::cloudMoveSpeed>,
           meta::field<&Self::cloudAlpha>,
           meta::field<&Self::epsilon>);
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
